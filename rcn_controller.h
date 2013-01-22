#ifndef RCN_CONTROLLER_H
#define RCN_CONTROLLER_H

#include <assert.h>

#include <rcn_node.h>

/// Set this to the number of supported channels before #including me
#ifndef RCN_CTRL_MAX_CHANNELS
#define RCN_CTRL_MAX_CHANNELS 1
#endif

#define LIMIT(min, val, max) (min > val ? min : (max < val) ? max : val)

const byte remote_host = 1; // RFM12B node ID of remote RCN node. TODO: Allow multiple remote hosts

class RCN_Controller
{
public:
	/*
	 * Whenever a channel is updated, either as a result of local
	 * action, or as a result of a status update from the host owning
	 * the channel, a callback function is invoked, allowing the user
	 * to be notified of the update. Since this notification happens
	 * regardless of the source of the update, the implementation of
	 * the callback function should probably not automatically cause
	 * further channel updates. Instead, the callback function is
	 * intended to provide feedback to the user of the channel update.
	 */
	typedef void (*update_notifier) (
		uint8_t channel, // The channel id
		uint8_t range, // The registered range for this channel
		uint8_t data, // The auxiliary data for this channel
		uint8_t old_level, // The old/previous level
		uint8_t new_level); // The new/current level

private:
	RCN_Node node;
	update_notifier notifier;
	size_t n_channels; // Number of active channels
	uint8_t range[RCN_CTRL_MAX_CHANNELS]; // channel ranges
	uint8_t level[RCN_CTRL_MAX_CHANNELS]; // channel levels
	uint8_t data[RCN_CTRL_MAX_CHANNELS]; // auxiliary channel data

	/// This is auto-invoked when a channel level changes.
	uint8_t update(uint8_t channel, int value)
	{
		assert(channel < n_channels);
		uint8_t v = LIMIT(0, value, range[channel]);
		notifier(channel, range[channel], data[channel],
			 level[channel], v);
		level[channel] = v;
		return v;
	}

public:
	RCN_Controller(
		uint8_t rf12_band, uint8_t rf12_group, uint8_t rf12_node,
		update_notifier notifier)
	: node(rf12_band, rf12_group, rf12_node),
	  notifier(notifier),
	  n_channels(0)
	{
	}

	void init()
	{
		node.init();
	}

	void add_channel(uint8_t r = 0xff, uint8_t l = 0, uint8_t d = 0)
	{
		assert(n_channels < RCN_CTRL_MAX_CHANNELS);
		size_t channel = n_channels++;
		range[channel] = r;
		level[channel] = l;
		data[channel] = d;
		update(channel, l);
		sync(channel);
	}

	size_t num_channels() const
	{
		return n_channels;
	}

	uint8_t get(uint8_t channel) const
	{
		assert(channel < n_channels);
		return level[channel];
	}

	/// Call this to request a status update from the remote host.
	void sync(uint8_t channel)
	{
		node.send_status_request(remote_host, channel);
	}

	/// Call this to change the absolute level of the given channel.
	uint8_t set(uint8_t channel, int value)
	{
		uint8_t ret = update(channel, value);
		// Send update request to remote host
		node.send_update_request_abs(remote_host, channel, ret);
		return ret;
	}

	/// Call this to relatively adjust the level of the given channel.
	uint8_t adjust(uint8_t channel, int delta)
	{
		int8_t d = LIMIT(-128, delta, 127);
		uint8_t ret = update(channel, level[channel] + delta);
		// Send update request to remote host
		if (d)
			node.send_update_request_rel(
				remote_host, channel, d);
		return ret;
	}

	/// Call this method often to keep things running smoothly.
	void run(void)
	{
		RCN_Node::RecvPacket p;
		if (!node.send_and_recv(p))
			return;

		if (p.channel() >= n_channels) {
#ifdef DEBUG
			LOG(F("Illegal channel number: "));
			LOGln(p.channel());
#endif
			return;
		}

		if (p.relative()) {
#ifdef DEBUG
			LOGln(F("Status update should not have relative "
				"level!"));
#endif
			return;
		}

#ifdef DEBUG
		LOG(F("Received status update for channel #"));
		LOG(p.channel());
		LOG(F(": "));
		LOG(get(p.channel()));
		LOG(F(" -> "));
		LOGln(p.abs_level());
#endif
		update(p.channel(), p.abs_level());
	}

	bool go_to_sleep()
	{
		return node.go_to_sleep();
	}

	/**
	 * Wake up from sleep
	 *
	 * Pass reset = true, if you want to temporarily reset cached
	 * levels to zero while waiting for status updates from host.
	 */
	void wake_up(bool reset = false)
	{
		node.wake_up();

		if (reset) {
			for (size_t i = 0; i < n_channels; i++)
				update(i, 0);
		}
	}
};

#endif // RCN_CONTROLLER_H
