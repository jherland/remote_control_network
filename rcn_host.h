#ifndef RCN_HOST_H
#define RCN_HOST_H

#include <assert.h>

#include <rcn_node.h>

/// Set this to the required number of channels before #including me
#ifndef RCN_HOST_MAX_CHANNELS
#define RCN_HOST_MAX_CHANNELS 1
#endif

#define LIMIT(min, val, max) (min > val ? min : (max < val) ? max : val)

class RCN_Host
{
public:
	/*
	 * Whenever a channel is about to be updated, the update is
	 * filtered through a callback function, which allows the user
	 * to react and/or change the update. All the registered details
	 * for the relevant channel is passed to this function, along with
	 * the current/old and proposed/new value. The function is
	 * expected to return the actual new value that will be stored.
	 *
	 * As such, if the function wishes to reject the new value, it
	 * merely returns 'old_level', while if it wishes to adopt the
	 * new value, it should return 'new_level'. Otherwise, any return
	 * value within 0 <= retval <= range is valid.
	 */
	typedef uint8_t (*update_filter) (
		uint8_t channel, // The channel id
		uint8_t range, // The registered range for this channel
		uint8_t data, // The auxiliary data for this channel
		uint8_t old_level, // The old/current level
		uint8_t new_level); // The proposed new level

private:
	RCN_Node node;
	update_filter handler;
	size_t num_channels; // Number of active channels
	uint8_t range[RCN_HOST_MAX_CHANNELS]; // channel ranges
	uint8_t level[RCN_HOST_MAX_CHANNELS]; // channel levels
	uint8_t data[RCN_HOST_MAX_CHANNELS]; // auxiliary channel data

public:
	RCN_Host(uint8_t rf12_band, uint8_t rf12_group, uint8_t rf12_node,
		update_filter handler)
	: node(rf12_band, rf12_group, rf12_node),
	  handler(handler),
	  num_channels(0)
	{
	}

	void init()
	{
		node.init();
	}

	void add_channel(uint8_t r = 0xff, uint8_t l = 0, uint8_t d = 0)
	{
		assert(num_channels < RCN_HOST_MAX_CHANNELS);
		size_t channel = num_channels++;
		range[channel] = r;
		data[channel] = d;
		set(channel, l);
	}

	uint8_t get(uint8_t channel) const
	{
		assert(channel < num_channels);
		return level[channel];
	}

	uint8_t set(uint8_t channel, int value)
	{
		assert(channel < num_channels);
		level[channel] = handler(
			channel,
			range[channel],
			data[channel],
			level[channel],
			LIMIT(0, value, range[channel])
		);
		node.send_status_update(channel, level[channel]);
		return level[channel];
	}

	uint8_t adjust(uint8_t channel, int delta)
	{
		int value = level[channel] + delta;
		return set(channel, value);
	}

	/// Call this method often to keep things running smoothly.
	void run(void)
	{
		RCN_Node::RecvPacket p;
		if (!node.send_and_recv(p))
			return;

		if (p.channel() >= num_channels) {
#ifdef DEBUG
			LOG(F("Illegal channel number: "));
			LOGln(p.channel());
#endif
			return;
		}

#ifdef DEBUG
		if (p.relative() && p.rel_level() == 0)
			LOG(F("Status request for"));
		else if (p.relative())
			LOG(F("Adjusting"));
		else
			LOG(F("Setting"));
		LOG(F(" channel #"));
		LOG(p.channel());
		LOG(F(": "));
		LOG(get(p.channel()));
		LOG(F(" + "));
		LOG(p.relative() ? p.rel_level() : p.abs_level());
		LOG(F(" => "));
#endif
		if (p.relative())
			adjust(p.channel(), p.rel_level());
		else
			set(p.channel(), p.abs_level());
#ifdef DEBUG
		LOGln(get(p.channel()));
#endif
	}
};

#endif // RCN_HOST_H
