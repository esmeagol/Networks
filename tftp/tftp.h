#ifndef _TFTP_H_
#define _TFTP_H_

#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TFTP_MAX_BUF_SIZE       516
#define TFTP_FILE_BLOCK_SIZE    512
#define TFTP_MAX_CLIENT_COUNT	10

#define TFTP_SELECT_TIMEOUT	500000	//u- seconds
#define TFTP_RETRY_INTERVAL	1	//seconds
#define TFTP_MAX_RETRY_TIME	32	//seconds
/* TFTP opcodes */
#define TFTP_OP_RRQ	1
#define TFTP_OP_WRQ	2
#define TFTP_OP_DATA	3
#define TFTP_OP_ACK	4
#define TFTP_OP_ERR	5

#if 0
/* structure containing encode and decode function pointers for each opcode */
typedef struct tftp_packet_handler_st
{
    short opcode;
    tftp_decode_fn* decode_fn;
    tftp_encode_fn* encode_fn;
} tftp_packet_handler_t;
#endif

/* structure containing client specific information */
typedef struct tftp_client_st
{
    int socket;
    short current_data_chunk_num;
    int current_retry_count;
    int current_retry_interval;
    short ack_num_expected;
    clock_t last_packet_sent_at;
    int final_packet_sent;
    char* file_name;
    int current_offset;
    struct sockaddr_in client;
    socklen_t client_len;
    void* buf;
    int buf_size;
 //   fpos_t pos;
} tftp_client_t;

/****************************************************************************
 *  encode / decode functions
 ***************************************************************************/
/* get information from buf recieved from client.
 *  we are decoding RRQ and ACK only */
int decode (void* buf,
	short* p_opcode,
	short* p_block_num,	// for ACK onl, ignore otherwise
	char** p_file_name,	// for RRQ onl, ignore otherwise
	char** p_mode		// for RRQ onl, ignore otherwise
	);

/* Convert information into buf. we are encoding data and error only */
int encode (short opcode,
	short block_num,        // for data only, ignore otherwise
	void* data,             // for data only, ignore otherwise
	int data_size,          // for data only, ignore otherwise
	short error_code,       // for error onl, ignore otherwise
	char* error_msg,        // for error onl, ignore otherwise
	void** p_out_buf,
	int* p_out_buf_size
	);

int tftp_rrq_handler (
	struct sockaddr_in client_addr,
	socklen_t client_addr_len,
	tftp_client_t** p_client_list,
	int* p_current_client_count,
	void* buf,
	int num_bytes_rcvd);

int tftp_ack_handler (
	struct sockaddr_in client_addr,
	socklen_t client_addr_len,
	int socket,
	tftp_client_t** p_client_list,
	int* p_current_client_count,
	void* buf,
	int num_bytes_rcvd);

/* define globals here */
int		g_max_fd;
long		g_ip_addr;
int		g_current_client_count;
fd_set          g_master_fds;
fd_set          g_read_fds;
clock_t		g_current_time;

#define TFTP_RETRY_START_INTERVAL	1 //sec
#define TFTP_RETRY_MAX_COUNT		5

#endif /* #ifndef _TFTP_H_ */
