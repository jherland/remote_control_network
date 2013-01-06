/*
 * Common definitions for the Remote Controller Network (RCN)
 *
 * This library implements a network of "hosts" and "controllers" on top
 * of a RFM12B network group.
 *
 * NOTE: This library depends on <RF12.h> being #included before this file.
 *
 * Objective
 * ---------
 *
 * The point of the RCN is to allow "controllers" to query and control
 * resources connected to "hosts", from across a RFM12B network. For
 * example, you may want to use a RFM12B-enabled battery-powered remote
 * control to adjust the volume of an A/V system, or adjust the intensity
 * of LED lightning controlled by a JeeNode or similar.
 *
 * Concepts
 * --------
 *
 * - Host: A host is a node in the network that administer one or more
 *   resources that can be queried and controlled by a Controller. The
 *   resources are made available to the Controllers as one or more
 *   Channels, each with an associated Level.
 *
 * - Controller: This is a node in the network that can query and control
 *   the current Level of a given Channel at a given Host.
 *
 * - Channel: This represents a controllable resource at a given Host in
 *   the network. Examples include the volume setting of an associated
 *   audio amplifier, the intensity of an associated LED light, the on/off
 *   state of an associated relay, etc. TODO: Expand the Channel concept
 *   with names, ranges, etc.
 *
 * - Level: This is the current value of a Channel, and is represented as
 *   a byte (an unsigned integer between 0 and 255). A Host administers
 *   one or more Channels, and keeps track of the current Level for each
 *   of them. A Controller may query the Host for the current Level of a
 *   given Channel, or it may request a change to the Level of a given
 *   Channel.
 *
 * Network configuration and discovery
 * -----------------------------------
 *
 * TODO
 *
 * Network operation
 * -----------------
 *
 * A Controller may at any point query the current Level of a given
 * Channel by sending a status request (SR) to the Host controlling that
 * Channel. The Host replies by broadcasting a status update (SU) to the
 * entire group. Additionally, when the Host for any reason changes the
 * current Level of a Channel, it should broadcast a status update (SU)
 * to the group, to allow all remote controllers to reflect the updated
 * Level.
 *
 * A Controller may at any point send an update request (UR) to a Host, to
 * request a change in the Level of a given Channel at that Host. The UR
 * may specify either a relative Level adjustment, or an absolute Level
 * setting. The Host may do whatever it wants with the UR (even ignoring
 * it completely), but it should always reply with another SU broadcast,
 * even if there is no actual Level change in the requested Channel.
 *
 * Upon receiving a status update (SU), the Controller should adjust any
 * internal cache it may have of the current Level og the given Channel.
 *
 * Network packet format
 * ---------------------
 *
 * The underlying RFM12B packet format looks like this (courtesy of
 * http://jeelabs.org/2011/01/14/nodes-addresses-and-interference/):
 *
 * 0xAA 0xAA 0xAA 0x2D GRP HDR LEN Data... CRC CRC
 *
 * 	- 0xAA    - preamble
 * 	- 0x2D    - sync
 * 	- GRP     - net group (RFM12: fixed, RFM12B: 1..250)
 * 	- HDR     - src/dst/ack packet header (CTL DST ACK Node_ID)
 * 	- LEN     - packet data length: 0..66
 * 	- Data... - Up to 66 bytes of payload
 * 	- CRC     - CRC16, little-endian
 *
 * So there is 9 bytes of overhead in addition to the payload data. Let's
 * look at what information needs to be in the packet. For the status
 * request (SR), we need to identify which Host and Channel to request
 * status for. The status update (SU) needs to identify the Host, the
 * Channel, and the current Level. Finally, the update request (UR) must
 * identify the Host and Channel, and then the requested (relative or
 * absolute) Level update to be applied. Note that the Host is already
 * part of the packet overhead (in case of a directed SR or UR message
 * from a Controller to a Host, the destination node ID (i.e. Host
 * address) is held in the packet's HDR byte, while in the case of a SU
 * broadcast from the Host, the source node ID (i.e. Host address) is
 * again held in the packet's HDR byte. Hence, the payload only needs to
 * contain the following information:
 *
 *  - The message type; SR, UR or SU
 *  - The Channel ID
 *  - In case of UR:
 *    - Whether the update is relative or absolute
 *    - The (relative or absolute) Level change
 *  - In case of SU:
 *    - The Level's current (absolute) value
 *
 * Note that since the Host should always reply with a SU to any UR (even
 * when the UR does not cause an actual change), we can mask the SR as an
 * UR requesting no change at all. I.e. to request the status of a given
 * Channel, we can simply send an update request with a relative value of
 * zero. This leaves us with two different message types: UR (which
 * is a directed message from Controller to Host) and SU (which is a
 * broadcast message from the Host). Now, the DST bit in the HDR byte
 * tells us whether a message is directed (and hence an UR) or a broadcast
 * (hence SU). Therefore, we only need to keep the following information
 * in the packet payload:
 *
 *   - The Channel ID (an unsigned integer large enough to uniquely
 *     identify all potential channels on a given Host)
 *   - The Level value (absolute in case of SU and relative or absolute in
 *     the case of UR)
 *   - A flag indicating whether (in the case of UR) the Level value is
 *     relative or absolute.
 *
 * Now, let's assume that we will not need more than 128 Channels on a
 * given Host, and that the Level value will be an absolute value between
 * 0..255. If we further limit any relative Level update to be within
 * -128..127, we can use the following 2-byte packet payload layout:
 *
 *   - Byte #1:
 *     - 1 bit: Relative flag; Set if Level is relative; Unset if absolute
 *     - Bits 6..0: Channel ID (0..127)
 *   - Byte #2:
 *     - Level value: Absolute (0..255) or relative (-128..127)
 *
 * To summarize, the packet payload looks like this for the various
 * message types:
 *
 *   - Status request (SR; Controller -> Host):
 *     - Relative flag: Set
 *     - Channel ID: $channel for which status is requested
 *     - Level value: 0 (No change to current value)
 *
 *   - Absolute update request (UR; Controller -> Host):
 *     - Relative flag: Unset
 *     - Channel ID: $channel for which Level should be updated
 *     - Level value: $abs_value (New absolute Level value)
 *
 *   - Relative update request (UR; Controller -> Host):
 *     - Relative flag: Set
 *     - Channel ID: $channel for which Level should be updated
 *     - Level value: $rel_value (Relative Level adjustment)
 *
 *   - Status Update (SU; broadcast from Host)
 *     - Relative flag: Unset
 *     - Channel ID: $channel for which the current Level is reported
 *     - Level value: $abs_value (The current value of the Level)
 *
 * Author: Johan Herland <johan@herland.net>
 * License: GNU GPL v2 or later
 */

#ifndef RCN_COMMON_H
#define RCN_COMMON_H

#ifndef RF12_h
#error You must #include <RF12.h> before #including this file
#endif

#include <Arduino.h>
#include <Print.h>

const unsigned int RCN_VERSION = 1;
const uint8_t RCN_SEND_BUF_SIZE = 16;

struct rfm12b_config {
	uint8_t node; // ID of this node (1..30)
	uint8_t band; // RF12_433MHZ, RF12_868MHZ or RF12_915MHZ
	uint8_t group; // Netgroup (1..212 for RFM12B, 212 for RFM12)
};

struct rcn_payload {
	uint8_t channel  : 7; // Channel ID
	uint8_t relative : 1; // Level is relative (=1) or absolute (=0).
	union {
		uint8_t abs_level;
		int8_t  rel_level;
	};
};

struct rcn_packet {
	uint8_t hdr; // RFM12B packer header
	union {
		struct rcn_payload d;
		uint8_t b[sizeof(struct rcn_payload)];
	};
};

// TODO: Consider making these instance variables of an RCN_Node class.
Print *rcn_logger;
const struct rfm12b_config *rfm12b_cfg;
struct rcn_packet rcn_send_buf[RCN_SEND_BUF_SIZE]; // ring buffer
uint8_t rcn_send_buf_next = 0; // producer adds packets at this index
uint8_t rcn_send_buf_done = 0; // consumer reads packets from this index

// Pass in Serial as logger
void rcn_init(const struct rfm12b_config *cfg, Print& logger)
{
	rcn_logger = &logger;
	rfm12b_cfg = cfg;
	rf12_initialize(cfg->node, cfg->band, cfg->group);

	rcn_logger->print(F("Initializing RCN v"));
	rcn_logger->print(RCN_VERSION);
	rcn_logger->print(F(", using RFM12B group.node "));
	rcn_logger->print(cfg->group);
	rcn_logger->print(F("."));
	rcn_logger->print(cfg->node);
	rcn_logger->print(F(" @ "));
	rcn_logger->print(cfg->band == RF12_868MHZ ? 868
		  : (cfg->band == RF12_433MHZ ? 433 : 915));
	rcn_logger->println(F("MHz"));
}

struct rcn_packet *rcn_prepare_send_packet(void)
{
	struct rcn_packet *p = rcn_send_buf + rcn_send_buf_next;
	// Advance producer index to next index w/wrap-around.
	++rcn_send_buf_next %= RCN_SEND_BUF_SIZE;
	// We should never overtake the consumer index.
	if (rcn_send_buf_next == rcn_send_buf_done)
		rcn_logger->println(F("Oops! OVerrunning rcn_send_buf!"));
	return p;
}

void rcn_send_status_update(uint8_t chan, uint8_t level)
{
	// Prepare broadcast packet with given data
	struct rcn_packet *p = rcn_prepare_send_packet();
	p->hdr = RF12_HDR_MASK & rfm12b_cfg->node;
	p->d.relative = 0;
	p->d.channel = chan;
	p->d.abs_level = level;
}

void rcn_send_update_request_abs(uint8_t host, uint8_t chan, uint8_t levl)
{
	// Prepare directed packet with given data
	struct rcn_packet *p = rcn_prepare_send_packet();
	p->hdr = RF12_HDR_DST | (RF12_HDR_MASK & host);
	p->d.relative = 0;
	p->d.channel = chan;
	p->d.abs_level = levl;
}

void rcn_send_update_request_rel(uint8_t host, uint8_t chan, int8_t adj)
{
	// Prepare directed packet with given data
	struct rcn_packet *p = rcn_prepare_send_packet();
	p->hdr = RF12_HDR_DST | (RF12_HDR_MASK & host);
	p->d.relative = 1;
	p->d.channel = chan;
	p->d.rel_level = adj;
}

void rcn_send_status_request(uint8_t host, uint8_t chan)
{
	rcn_send_update_request_rel(host, chan, 0);
}

#ifdef DEBUG
void rcn_print_byte_buf(const volatile uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		rcn_logger->print(F(" "));
		if (buf[i] <= 0xf)
			rcn_logger->print(F("0"));
		rcn_logger->print(buf[i], HEX);
	}
	rcn_logger->println();
}
#endif

const struct rcn_payload *rcn_send_and_recv(void)
{
	if (rcn_send_buf_next != rcn_send_buf_done && rf12_canSend()) {
		// We have packets to send, and we can send them...
		struct rcn_packet * p = rcn_send_buf + rcn_send_buf_done;
		++rcn_send_buf_done %= RCN_SEND_BUF_SIZE;
		rf12_sendStart(p->hdr, p->b, sizeof(p->b));

#ifdef DEBUG
		rcn_logger->print(F("rcn_send_and_recv(): Sending "));
		rcn_logger->print((p->hdr & RF12_HDR_DST)
			? F("message to node ")
			: F("broadcast from node "));
		rcn_logger->print(p->hdr & RF12_HDR_MASK);
		rcn_logger->print(": ");
		rcn_print_byte_buf(p->b, sizeof(p->b));
#endif
	}

	if (rf12_recvDone()) {
		if (rf12_crc) {
#ifdef DEBUG
			rcn_logger->println(F("rcn_send_and_recv(): "
				"Dropping packet with CRC mismatch!"));
#endif
			return NULL;
		}
#ifdef DEBUG
		rcn_logger->print(F("rcn_send_and_recv(): Received "));
		rcn_logger->print((rf12_hdr & RF12_HDR_DST)
			? F("message") : F("broadcast"));
		rcn_logger->print(F(" from node "));
		rcn_logger->print(rf12_hdr & RF12_HDR_MASK);
		rcn_logger->print(": ");
		rcn_print_byte_buf(rf12_data, rf12_len);
#endif
		if (rf12_len != sizeof(struct rcn_payload))
			return NULL;
		return (struct rcn_payload *) rf12_data;
	}
	return NULL;
}

#endif // RCN_COMMON_H
