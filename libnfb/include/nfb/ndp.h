/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb public header file - NDP module
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef LIBNFB_NDP_H
#define LIBNFB_NDP_H

#ifndef __KERNEL__
#include <stdint.h>
#endif
#include <nfb/ext.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ~~~~[ INCLUDES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* ~~~~[ DEFINES and MACROS ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* ~~~~[ TYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */


struct nfb_device;
struct ndp_queue;

/*!
 * \brief Opaque datatype for NDP RX queue
 */
typedef struct ndp_queue ndp_rx_queue_t;

/*!
 * \brief Opaque datatype for NDP TX queue
 */
typedef struct ndp_queue ndp_tx_queue_t;

/*!
 * \brief NDP packet
 *
 * NDP packet contains packet data and metadata (header).
 *
 * \warning The data are not allocated when this 'struct' is created, they are
 *          still in their original locations, so proper care must be taken when
 *          working with NDP packets (especially, one must NOT assume that the
 *          data is available as long as this 'struct' is available).
 */
struct ndp_packet {
	unsigned char *data;        //!< Packet data location
	unsigned char *header;      //!< Packet metadata location
	uint32_t data_length;       //!< Packet data length
	uint16_t header_length;     //!< Packet metadata length
	uint16_t flags;             //!< Packet specific flags
};

/* For TX must be called before ndp_tx_burst_get is issued */
static inline void ndp_packet_flag_header_id_set(struct ndp_packet *p, uint8_t id)
{
	p->flags = (p->flags & ~0x3) | (id & 0x3);
}

static inline uint8_t ndp_packet_flag_header_id_get(const struct ndp_packet *p)
{
	return p->flags & 0x3;
}

/*!
 * \brief Library error codes
 */
enum ndp_error {
	NDP_OK = 0,
};

/*!
 * \brief NDP frame printing options
 *
 * Use logical OR of the values to choose the data to be printed.
 * \ref NDP_PRINT_ALL always prints everything there is to print.
 */
enum ndp_print_option {
	NDP_PRINT_INFO      = 1 << 0, /*!< Print frame information (queue ID, lengths) */
	NDP_PRINT_METADATA  = 1 << 1, /*!< Print packet metadata */
	NDP_PRINT_DATA      = 1 << 2, /*!< Print packet data */
	NDP_PRINT_ALL       = 0xFF    /*!< Print all information you can */
};

/*!
 * \brief NDP queue opening flags
 */
typedef int ndp_open_flags_t;
#define NDP_OPEN_FLAG_NO_BUFFER (1 <<  0) /*!< Open queue (RX or TX) in NO_BUFFER mode where packet data space is supplied by the user and not by the driver */
#define NDP_OPEN_FLAG_USERSPACE (1 <<  1)

/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */


/*! ---- QUEUE ---------------------------------------------------------------
 * @defgroup queue_fn Queue related functions
 * @{
 */

/*!
 * \brief Open (subscribe) NDP RX queue
 * \param[in] nfb       NFB device
 * \param[in] queue_id  Queue ID
 * \param[in] flags     TX queue openning flags
 * \return NDP RX queue datatype on success, NULL on error
 */
ndp_rx_queue_t *ndp_open_rx_queue_ext(struct nfb_device *nfb, unsigned queue_id, ndp_open_flags_t flags);

/*!
 * \brief Open (subscribe) NDP RX queue
 * \param[in] nfb       NFB device
 * \param[in] queue_id  Queue ID
 * \return NDP RX queue datatype on success, NULL on error
 */
ndp_rx_queue_t *ndp_open_rx_queue(struct nfb_device *nfb, unsigned queue_id);

/*!
 * \brief Open (subscribe) NDP TX queue
 * \param[in] nfb       NFB device
 * \param[in] queue_id  Queue ID
 * \param[in] flags     TX queue openning flags
 * \return NDP TX queue datatype on success, NULL on error
 */
ndp_tx_queue_t *ndp_open_tx_queue_ext(struct nfb_device *nfb, unsigned queue_id, ndp_open_flags_t flags);

/*!
 * \brief Open (subscribe) NDP TX queue
 * \param[in] nfb       NFB device
 * \param[in] queue_id  Queue ID
 * \return NDP TX queue datatype on success, NULL on error
 */
ndp_tx_queue_t *ndp_open_tx_queue(struct nfb_device *nfb, unsigned queue_id);

/*!
 * \brief Close (unsubscribe) NDP RX queue
 * \param[in] queue  NDP RX queue
 *
 * The queue must not be used after this function is called and should be set
 * to NULL by the application.
 */
void ndp_close_rx_queue(ndp_rx_queue_t *queue);

/*!
 * \brief Close (unsubscribe) NDP TX queue
 * \param[in] queue  NDP TX queue
 *
 * The queue must not be used after this function is called and should be set
 * to NULL by the application.
 */
void ndp_close_tx_queue(ndp_tx_queue_t *queue);

/*!
 * \brief Start traffic on queue
 * \param[in] queue  NDP queue
 * \return NDP_OK on success, nonzero error code otherwise
 *
 * When this function is called,
 * RX queue become able to receive packets
 * (don't mistake this with RXMAC control), or
 * TX queue become able to transmit packets.
 */
int ndp_queue_start(struct ndp_queue *queue);

/*!
 * \brief Stop traffic on queue
 * \param[in] queue     NDP queue
 * \return NDP_OK on success, nonzero error code otherwise
 *
 * When this function is called, it stops the delivery of packets to the queue.
 */
int ndp_queue_stop(struct ndp_queue *queue);

/*!
 * \brief Aquire buffer size for queue in number of NDP packets
 * \param[in] q         NDP queue
 * \return Buffer size for queue version 2; value 0 for version 1
 *
 */
unsigned ndp_get_buffer_item_count(ndp_tx_queue_t *q);

/*! @} */ // end of group

/*! ---- RX (PACKET READING) ---------------------------------------------------
 * @defgroup rx_functions Packet reading functions
 *
 * These functions allow user to read either NDP frames (decoded packet data and metadata).
 *
 * @{
 */

/*!
 * \brief Get (read) burst of NDP packets from NDP RX queue
 * \param[in]  queue    NDP RX queue
 * \param[out] packets  Array of NDP packet structs
 * \param[in]  count    Maximal count of packets to read (length of \p packets)
 * \return Count of actually read packets
 *
 * \note Traffic must be started (see \ref ndp_queue_start)
 *
 * When the function returns nonzero, the NDP packet structs in \p packets are
 * filled with pointers to packet data. This data is valid until
 * \ref ndp_rx_burst_put is called.
 *
 * The packet data must eventually be released through \ref ndp_rx_burst_put.
 * The usage should look like this
 *
 * \code
 * struct ndp_packet packets[32];
 * unsigned count = 32;
 * struct ndp_packet more_packets[16];
 * unsigned more_count = 16;
 *
 * while (!STOPPED) {
 *   unsigned nb_rx = ndp_rx_burst_get(queue, packets, count);
 *   // process packets
 *   if (condition) {
 *     nb_rx += ndp_rx_burst_get(queue, more_packets, more_count); // this is OK
 *     // process more packets
 *   }
 *   ndp_rx_burst_put(queue); // but this should be called "soon enough"
 * }
 * \endcode
 */
unsigned ndp_rx_burst_get(ndp_rx_queue_t *queue, struct ndp_packet *packets, unsigned count);

/*!
 * \brief Put back (end reading) bursts of NDP packets from NDP queue
 * \param[in] queue  NDP RX queue
 *
 * This function should be called when the application has finished processing
 * the packets obtained by previous \ref ndp_rx_burst_get on the same queue.
 * The amount of data available for processing is limited, so this function
 * should best be called after \b every \ref ndp_rx_burst_get.
 *
 * \see ndp_rx_burst_get for an example how to use this API.
 */
void ndp_rx_burst_put(ndp_rx_queue_t *queue);

/*!
 * \brief Insert a set of user-defined NDP packet placeholders to NDP queue in NDP_CHANNEL_FLAG_NO_BUFFER mode
 * \param[in] q       NDP queue in NDP_CHANNEL_FLAG_NO_BUFFER mode
 * \param[in] packets Array of NDP packet structs with <b>length, flag next and data pointer filled</b>
 * \param[in] count   Requested number of packets to insert (length of \p packets)
 * \return Number of successfully inserted packets
 */
unsigned ndp_rx_burst_put_desc(struct ndp_queue *q, struct ndp_packet *packets, unsigned count);

/*! @} */ // end of group

/*! ---- TX (PACKET WRITING) ---------------------------------------------------
 * @defgroup tx_functions Packet writing functions
 *
 * For transmitting packets, they must be placed to the appropriate position in
 * the SZE buffer. As a result, the transmission is logically divided into two
 * operations - allocating space in the SZE buffer and filling it with data.
 *
 * All the writing operations are burst-oriented - the packets might not be sent
 * when the functions return, the API user must check this.
 *
 * @{
 */

/*!
 * \brief Write a burst of NDP packets to NDP TX queue with data copying
 * \param[in] queue    NDP TX queue
 * \param[in] packets  NDP packet structs filled with packet data
 * \param[in] count    Count of packets to write
 * \return Count of actually written packets
 *
 * This is the slower but simpler way to transmit data. The structs in
 * \p packets must contain full packet information, the data (and metadata) will
 * be copied from the packets to the NDP buffer.
 *
 * API example:
 * \code
 * struct ndp_packet packets[32];
 * unsigned count = 32;
 *
 * while (!STOPPED) {
 *     unsigned nb_rx = ndp_rx_burst_get(rx_queue, packets, count);
 *     // process packets
 *     unsigned nb_tx = ndp_tx_burst_copy(tx_queue, packets, nb_rx);
 * }
 * \endcode
 */
unsigned ndp_tx_burst_copy(ndp_tx_queue_t *queue, struct ndp_packet *packets, unsigned count);

/*!
 * \brief Get burst of packet placeholders from NDP TX queue
 * \param[in]    queue    NDP TX queue
 * \param[inout] packets  NDP packet structs with <b>only sizes filled</b>
 * \param[in]    count    Maximal count of retrieved placeholders (length of \p packets)
 * \return Count of actually retrieved placeholders.
 *         Current implemenation returns either requested \p count or zero; no values in between.
 *
 * This function allocates NDP frame \b placeholders in the TX queue and sets the
 * pointers to them to the structs in \p packets parameter. To do this, the
 * user \b MUST fill the \ref ndp_packet.data_length field of the structs in
 * \p packets and <b>MUST NOT</b> fill the \ref ndp_packet.data field, as the pointer
 * is invalid and will be usable after the function returns successfully.
 *
 * The user is expected to fill the required data lengths in the \p packets array,
 * call this function to allocate placeholders, fill placeholders with packet
 * data and finally call \ref ndp_tx_burst_put to send the burst.
 *
 * API example:
 * \code
 * struct ndp_packet rx_packets[32], tx_packets[32];
 * unsigned count = 32;
 *
 * while (!STOPPED) {
 *   unsigned nb_rx = nb_rx_burst_get(rx_queue, rx_packets, count);
 *
 *   // fill structs with requested packet lengths
 *   for(int i = 0; i < nb_rx; i++) {
 *     tx_packets[i].data_length = rx_packets[i].data_length;
 *   }
 *
 *   // get placeholders of requested lengths
 *   unsigned nb_tx = nb_tx_burst_get(tx_queue, tx_packets, nb_rx);
 *
 *   // write data to placeholders
 *   for(int i = 0; i < nb_tx; i++) {
 *     memcpy(tx_packets[i].data, rx_packets[i].data, tx_packets[i].data_len);
 *   }
 *
 *   nb_tx_burst_put(tx_queue);
 *   nb_rx_burst_put(rx_queue);
 * }
 * \endcode
 */
unsigned ndp_tx_burst_get(ndp_tx_queue_t *queue, struct ndp_packet *packets, unsigned count);

/*!
 * \brief Put back (write) bursts of NDP packets to NDP TX queue
 * \param[in] queue  NDP TX queue
 *
 * This function must be called after (1 or more) call to \ref ndp_tx_burst_get
 * to notify the library that the bursts were processed and can be written.
 *
 * See \ref ndp_rx_burst_put for the API usage and caveats.
 *
 * Also be aware, that the library tries to hold NDP packets for best performance, thus they
 * doesn't have to be send immediately. See \ref ndp_tx_burst_flush.
 */
void ndp_tx_burst_put(ndp_tx_queue_t *queue);

/*!
 * \brief Put back (write) bursts of NDP packets to NDP TX queue and send immediately
 * \param[in] queue  NDP TX queue
 *
 * Can be directly used instead of ndp_tx_burst_put, but is significantly slower.
 */
void ndp_tx_burst_flush(ndp_tx_queue_t *queue);

/*!
 * \brief Read number of available free slots in transmition buffer
 * \param[in] q      NDP TX queue
 * \return Number of free slots for queue version 2; value 0 for other versions
 *
 */
unsigned ndp_v2_tx_get_pkts_available(struct ndp_queue *q);

/*! @} */ // end of group: write (TX) functions

/*! ---- INFO ------------------------------------------------------------------
 * @defgroup info_functions Functions providing NDP queue information
 * @{
 */

/*!
 * \brief Get number of NDP queue NUMA node
 * \param[in] queue  NDP queue
 * \return NUMA node number on success, negative error code otherwise
 */
int ndp_queue_get_numa_node(const struct ndp_queue *queue);

/*!
 * \brief Get number of RX queues available in NFB device
 * \param[in] nfb  NFB device
 * \return Number of RX queues available on success, negative error code otherwise
 */
int ndp_get_rx_queue_count(const struct nfb_device *nfb);
int ndp_get_rx_queue_available_count(const struct nfb_device *dev);
int ndp_rx_queue_is_available(const struct nfb_device *dev, unsigned index);

/*!
 * \brief Get number of TX queues available in NFB device
 * \param[in] nfb  NFB device
 * \return Number of TX queues available on success, negative error code otherwise
 */
int ndp_get_tx_queue_count(const struct nfb_device *nfb);
int ndp_get_tx_queue_available_count(const struct nfb_device *dev);
int ndp_tx_queue_is_available(const struct nfb_device *dev, unsigned index);

/*! @} */ // end of group: info functions

/*! ---- AUXILIARY -------------------------------------------------------------
 * @defgroup aux_functions Auxiliary functions
 *
 * @{
 */

/*!
 * \brief Decode received NDP frame into NDP packet
 * \param[in]  frame   NDP frame
 * \param[out] packet  Pointer to caller-owned 'NDP packet' structure
 * \return NDP_OK on success, nonzero error code otherwise
 */
int ndp_decode_packet(char *frame, struct ndp_packet *packet);

/*!
 * \brief Print NDP packet data to standard output
 * \param[in] packet         NDP packet
 * \param[in] print_options  Print options
 * \return NDP_OK on success, nonzero error code otherwise
 *
 * The print_options parameter is 1 or more OR-combined NDP_PRINT_* values
 * of the \ref ndp_print_option type.
 */
int ndp_print_packet(const struct ndp_packet *packet, unsigned print_options);

/*! @} */ // end of group: auxiliary functions

int ndp_rx_poll(struct nfb_device *dev, int timeout, struct ndp_queue **queue);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* LIBNFB_NDP_H */
