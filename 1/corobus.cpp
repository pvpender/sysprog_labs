#include "corobus.h"

#include "libcoro.h"
#include "rlist.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <queue>
#include <vector>
#include <optional>


/**
 * One coroutine waiting to be woken up in a list of other
 * suspended coros.
 */
struct wakeup_entry {
	struct rlist base;
	struct coro *coro;
};

/** A queue of suspended coros waiting to be woken up. */
struct wakeup_queue {
	struct rlist coros;
};

#if 0 /* Uncomment this if want to use */

/** Suspend the current coroutine until it is woken up. */
static void
wakeup_queue_suspend_this(struct wakeup_queue *queue)
{
	struct wakeup_entry entry;
	entry.coro = coro_this();
	rlist_add_tail_entry(&queue->coros, &entry, base);
	coro_suspend();
	rlist_del_entry(&entry, base);
}

/** Wakeup the first coroutine in the queue. */
static void
wakeup_queue_wakeup_first(struct wakeup_queue *queue)
{
	if (rlist_empty(&queue->coros))
		return;
	struct wakeup_entry *entry = rlist_first_entry(&queue->coros,
		struct wakeup_entry, base);
	coro_wakeup(entry->coro);
}

#endif

struct coro_bus_channel {
	/** Channel max capacity. */
	size_t size_limit;
	/** Coroutines waiting until the channel is not full. */
	std::queue<coro*> send_queue;
	/** Coroutines waiting until the channel is not empty. */
	std::queue<coro*> recv_queue;
	/** Message queue. */
	/* std::vector/queue/deque/list/...<unsigned> data; */
	std::queue<unsigned> data;
};

struct coro_bus {
	std::vector<std::optional<coro_bus_channel>> channels;
	int channel_count;
};

static enum coro_bus_error_code global_error = CORO_BUS_ERR_NONE;

enum coro_bus_error_code
coro_bus_errno(void)
{
	return global_error;
}

void
coro_bus_errno_set(enum coro_bus_error_code err)
{
	global_error = err;
}

struct coro_bus *
coro_bus_new(void)
{
	coro_bus *new_coro_bus = new coro_bus{.channels=std::vector<std::optional<coro_bus_channel>>(), .channel_count=0};
	return new_coro_bus;
}

void
coro_bus_delete(struct coro_bus *bus)
{
	for (auto i = 0; i < bus->channel_count; ++i) {
		coro_bus_channel_close(bus, i);
	}
	
	bus->channel_count = 0;

	delete bus;
}

int
coro_bus_channel_open(struct coro_bus *bus, size_t size_limit)
{
	/* IMPLEMENT THIS FUNCTION */
	/*
	 * One of the tests will force you to reuse the channel
	 * descriptors. It means, that if your maximal channel
	 * descriptor is N, and you have any free descriptor in
	 * the range 0-N, then you should open the new channel on
	 * that old descriptor.
	 *
	 * A more precise instruction - check if any of the
	 * bus->channels[i] with i = 0 -> bus->channel_count is
	 * free (== NULL). If yes - reuse the slot. Don't grow the
	 * bus->channels array, when have space in it.
	 */

	if (bus->channel_count > 0) {
		for (auto i = 0; i < bus->channel_count; ++i) {
			if (!bus->channels.at(i).has_value()) {
				bus->channels[i] = std::make_optional<coro_bus_channel>(coro_bus_channel{
					.size_limit = size_limit,
					.send_queue = std::queue<coro*>(),
					.recv_queue = std::queue<coro*>(),
					.data = std::queue<unsigned>()
				});

				return i;
			}
		}
	}

	bus->channels.emplace_back(std::in_place, size_limit, std::queue<coro*>(), std::queue<coro*>(), std::queue<unsigned>());
	++bus->channel_count;
	
	return bus->channel_count - 1;
}

void
coro_bus_channel_close(struct coro_bus *bus, int channel)
{
	/*
	 * Be very attentive here. What happens, if the channel is
	 * closed while there are coroutines waiting on it? For
	 * example, the channel was empty, and some coros were
	 * waiting on its recv_queue.
	 *
	 * If you wakeup those coroutines and just delete the
	 * channel right away, then those waiting coroutines might
	 * on wakeup try to reference invalid memory.
	 *
	 * Can happen, for example, if you use an intrusive list
	 * (rlist), delete the list itself (by deleting the
	 * channel), and then the coroutines on wakeup would try
	 * to remove themselves from the already destroyed list.
	 *
	 * Think how you could address that. Remove all the
	 * waiters from the list before freeing it? Yield this
	 * coroutine after waking up the waiters but before
	 * freeing the channel, so the waiters could safely leave?
	 */

	if ((channel >= bus->channel_count) || (!bus->channels.at(channel).has_value())) {
		return;
	}

	auto &chan = bus->channels.at(channel);

	while (!chan->send_queue.empty()) {
		coro_wakeup(chan->send_queue.front());
		chan->send_queue.pop();
	}

	while(!chan->recv_queue.empty()) {
		coro_wakeup(chan->recv_queue.front());
		chan->recv_queue.pop();
	}

	bus->channels[channel].reset();
}

int
coro_bus_send(struct coro_bus *bus, int channel, unsigned data)
{
	if ((channel >= bus->channel_count) || (!bus->channels.at(channel).has_value())) {
		coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
		return -1;
	}

	auto &chan = bus->channels.at(channel);

	while (true) {

		if (!chan.has_value()) {
			coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
			return -1;
		}

		if (chan->data.size() == chan->size_limit) {
			struct coro *current = coro_this();
			chan->send_queue.push(current);
			coro_suspend();
			continue;
		}

		chan->data.push(data);

		if (!chan->data.empty() && !chan->recv_queue.empty()) {
			struct coro *next_recv = chan->recv_queue.front();
			chan->recv_queue.pop();
			coro_wakeup(next_recv);
		}

		return 0;
	}
	/*
	 * Try sending in a loop, until success. If error, then
	 * check which one is that. If 'wouldblock', then suspend
	 * this coroutine and try again when woken up.
	 *
	 * If see the channel has space, then wakeup the first
	 * coro in the send-queue. That is needed so when there is
	 * enough space for many messages, and many coroutines are
	 * waiting, they would then wake each other up one by one
	 * as lone as there is still space.
	 */
}

int
coro_bus_try_send(struct coro_bus *bus, int channel, unsigned data)
{
	if ((channel >= bus->channel_count) || (!bus->channels.at(channel).has_value())) {
		coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
		return -1;
	}

	auto &chan = bus->channels.at(channel);

	if (chan->data.size() == chan->size_limit) {
		coro_bus_errno_set(CORO_BUS_ERR_WOULD_BLOCK);
		return -1;
	}

	chan->data.push(data);

	if (!chan->data.empty() && !chan->recv_queue.empty()) {
		struct coro *next_recv = chan->recv_queue.front();
		chan->recv_queue.pop();
		coro_wakeup(next_recv);
	}

	return 0;

	/*
	 * Append data if has space. Otherwise 'wouldblock' error.
	 * Wakeup the first coro in the recv-queue! To let it know
	 * there is data.
	 */
}

int
coro_bus_recv(struct coro_bus *bus, int channel, unsigned *data)
{
	if ((channel >= bus->channel_count) || (!bus->channels.at(channel).has_value())) {
		coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
		return -1;
	}

	auto &chan = bus->channels.at(channel);

	while (true) {

		if (!chan.has_value()) {
			coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
			return -1;
		}

		if (chan->data.empty()) {
			struct coro *current = coro_this();
			chan->recv_queue.push(current);
			coro_suspend();
			continue;
		}

		*data = chan->data.front();
		chan->data.pop();

		if (!chan->send_queue.empty() && (chan->data.size() < chan->size_limit)) {
			struct coro *next_send = chan->send_queue.front();
			chan->send_queue.pop();
			coro_wakeup(next_send);
		}

		return 0;
	}
}

int
coro_bus_try_recv(struct coro_bus *bus, int channel, unsigned *data)
{
	if ((channel >= bus->channel_count) || (!bus->channels.at(channel).has_value())) {
		coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
		return -1;
	}

	auto &chan = bus->channels.at(channel);

	if (chan->data.empty()) {
		coro_bus_errno_set(CORO_BUS_ERR_WOULD_BLOCK);
		return -1;
	}

	*data = chan->data.front();
	chan->data.pop();

	if (!chan->send_queue.empty() && (chan->data.size() < chan->size_limit)) {
		struct coro *next_send = chan->send_queue.front();
		chan->send_queue.pop();
		coro_wakeup(next_send);
	}

	return 0;
}


#if NEED_BROADCAST

int
coro_bus_broadcast(struct coro_bus *bus, unsigned data)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)data;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

int
coro_bus_try_broadcast(struct coro_bus *bus, unsigned data)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)data;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

#endif

#if NEED_BATCH

int
coro_bus_send_v(struct coro_bus *bus, int channel, const unsigned *data, unsigned count)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)channel;
	(void)data;
	(void)count;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

int
coro_bus_try_send_v(struct coro_bus *bus, int channel, const unsigned *data, unsigned count)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)channel;
	(void)data;
	(void)count;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

int
coro_bus_recv_v(struct coro_bus *bus, int channel, unsigned *data, unsigned capacity)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)channel;
	(void)data;
	(void)capacity;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

int
coro_bus_try_recv_v(struct coro_bus *bus, int channel, unsigned *data, unsigned capacity)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)channel;
	(void)data;
	(void)capacity;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

#endif
