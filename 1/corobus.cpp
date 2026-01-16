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
	bool is_any_channel_presented, exists_blocked_chan = false;
	std::optional<coro_bus_channel> *blocked_chan;

	for (auto &chan: bus->channels) {
		if (chan.has_value()) {
			is_any_channel_presented = true;
			break;
		}
	}

	if (!is_any_channel_presented) {
		coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
		return -1;
	}

	while (true)
	{
		exists_blocked_chan = false;
		is_any_channel_presented = false;

		for (auto &chan: bus->channels) {
			if (!chan.has_value())
				continue;

			is_any_channel_presented = true;

			if (chan->data.size() == chan->size_limit) {
				exists_blocked_chan = true;
				blocked_chan = &chan;
				break;
			}
		}

		if (!is_any_channel_presented) {
			coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
			return -1;
		}

		if (exists_blocked_chan) {
			struct coro *current = coro_this();
			(*blocked_chan)->send_queue.push(current);
			coro_suspend();
			continue;
		}

		for (auto &chan: bus->channels) {
			if (!chan.has_value())
				continue;
			
			chan->data.push(data);
			if (!chan->recv_queue.empty()) {
				struct coro *next_recv = chan->recv_queue.front();
				chan->recv_queue.pop();
				coro_wakeup(next_recv);
			}
		}

		return 0;
	}
}

int
coro_bus_try_broadcast(struct coro_bus *bus, unsigned data)
{
	bool is_any_channel_presented, exists_blocked_chan = false;

	for (auto &chan: bus->channels) {
		if (chan.has_value()) {
			is_any_channel_presented = true;
			break;
		}
	}

	if (!is_any_channel_presented) {
		coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
		return -1;
	}

	exists_blocked_chan = false;
	is_any_channel_presented = false;

	for (auto &chan: bus->channels) {
		if (!chan.has_value())
			continue;

		is_any_channel_presented = true;

		if (chan->data.size() == chan->size_limit) {
			exists_blocked_chan = true;
			break;
		}
	}

	if (!is_any_channel_presented) {
		coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
		return -1;
	}

	if (exists_blocked_chan) {
		coro_bus_errno_set(CORO_BUS_ERR_WOULD_BLOCK);
		return -1;
	}

	for (auto &chan: bus->channels) {
		if (!chan.has_value())
			continue;
			
		chan->data.push(data);
		if (!chan->recv_queue.empty()) {
			struct coro *next_recv = chan->recv_queue.front();
			chan->recv_queue.pop();
			coro_wakeup(next_recv);
		}
	}

	return 0;
}

#endif

#if NEED_BATCH

int
coro_bus_send_v(struct coro_bus *bus, int channel, const unsigned *data, unsigned count)
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

		size_t sended_messages = 0;

		while ((sended_messages != count) && (chan->data.size() != chan->size_limit)) {
			chan->data.push(*(data + sended_messages));
			++sended_messages;
		}

		if (!chan->data.empty() && !chan->recv_queue.empty()) {
			struct coro *next_recv = chan->recv_queue.front();
			chan->recv_queue.pop();
			coro_wakeup(next_recv);
		}

		return sended_messages;
	}
}

int
coro_bus_try_send_v(struct coro_bus *bus, int channel, const unsigned *data, unsigned count)
{
	if ((channel >= bus->channel_count) || (!bus->channels.at(channel).has_value())) {
		coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
		return -1;
	}

	auto &chan = bus->channels.at(channel);

	if (!chan.has_value()) {
		coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
		return -1;
	}

	if (chan->data.size() == chan->size_limit) {
		coro_bus_errno_set(CORO_BUS_ERR_WOULD_BLOCK);
		return - 1;
	}

	size_t sended_messages = 0;

	while ((sended_messages != count) && (chan->data.size() != chan->size_limit)) {
		chan->data.push(*(data + sended_messages));
		++sended_messages;
	}

	if (!chan->data.empty() && !chan->recv_queue.empty()) {
		struct coro *next_recv = chan->recv_queue.front();
		chan->recv_queue.pop();
		coro_wakeup(next_recv);
	}

	return sended_messages;
}

int
coro_bus_recv_v(struct coro_bus *bus, int channel, unsigned *data, unsigned capacity)
{
	if ((channel >= bus->channel_count) || (!bus->channels.at(channel).has_value())) {
		coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
		return -1;
	}

	auto &chan = bus->channels.at(channel);

	while (true)
	{
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

		size_t recv_messages = 0;

		while (!chan->data.empty() && (recv_messages != capacity)) {
			*(data + recv_messages) = chan->data.front();
			chan->data.pop();
			++recv_messages;
		}

		if (!chan->send_queue.empty() && (chan->data.size() < chan->size_limit)) {
			struct coro *next_send = chan->send_queue.front();
			chan->send_queue.pop();
			coro_wakeup(next_send);
		}

		return recv_messages;
	}
}

int
coro_bus_try_recv_v(struct coro_bus *bus, int channel, unsigned *data, unsigned capacity)
{
	if ((channel >= bus->channel_count) || (!bus->channels.at(channel).has_value())) {
		coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
		return -1;
	}

	auto &chan = bus->channels.at(channel);

	if (!chan.has_value()) {
		coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
		return -1;
	}

	if (chan->data.empty()) {
		coro_bus_errno_set(CORO_BUS_ERR_WOULD_BLOCK);
		return -1;
	}

	size_t recv_messages = 0;

	while (!chan->data.empty() && (recv_messages != capacity)) {
		*(data + recv_messages) = chan->data.front();
		chan->data.pop();
		++recv_messages;
	}

	if (!chan->send_queue.empty() && (chan->data.size() < chan->size_limit)) {
		struct coro *next_send = chan->send_queue.front();
		chan->send_queue.pop();
		coro_wakeup(next_send);
	}

	return recv_messages;
}

#endif
