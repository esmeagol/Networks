#include "tftp.h"

/* Convert information into buf. we are encoding data and error only */
int encode (short opcode,
	short block_num,	// for data only, ignore otherwise
	void* data,		// for data only, ignore otherwise
	int data_size,		// for data only, ignore otherwise
	short error_code,	// for error onl, ignore otherwise
	char* error_msg,	// for error onl, ignore otherwise
	void** p_out_buf,
	int* p_out_buf_size
	)
{
    int buf_size = 0;
    int result = 0;
    void* temp = NULL;
    short n_opcode = htons (opcode);
    short n_block_num = htons (block_num);

    if (!p_out_buf)
    {
	printf ("invalid_pointer p_out_buf. \n");
	result = -1;
	goto out;
    }

    *p_out_buf = NULL;

    switch (opcode)
    {
	case TFTP_OP_DATA:
	    buf_size = sizeof (opcode) + sizeof (n_block_num) + data_size;
	    *p_out_buf_size = buf_size;

	    *p_out_buf = malloc (buf_size);
	    if (!*p_out_buf)
	    {
		printf ("could not allocate buf. \n");
		result = -1;
		goto out;
	    }
	    memset (*p_out_buf, 0, buf_size);

	    temp = *p_out_buf;

	    /* copy opcode */
	    memcpy (temp, &n_opcode, sizeof (n_opcode));
	    temp = (char*) temp + sizeof (n_opcode);

	    memcpy (temp, &n_block_num, sizeof (n_block_num));
	    temp = (char*) temp + sizeof (n_block_num);

	    memcpy (temp, data, data_size);
	    break;

	case TFTP_OP_ERR:
	    buf_size = sizeof (n_opcode) + sizeof (error_code) +
		strlen (error_msg) + 1 + 1;
	    *p_out_buf_size = buf_size;

	    *p_out_buf = malloc (buf_size);
	    if (!*p_out_buf)
	    {
		printf ("could not allocate buf. \n");
		result = -1;
		goto out;
	    }
	    memset (*p_out_buf, 0, buf_size);

	    temp = *p_out_buf;

	    /* copy
	     * opcode
	     * */
	    memcpy (temp, &n_opcode, sizeof (n_opcode));
	    temp = (char*) temp + sizeof (n_opcode);

	    memcpy (temp, &error_code, sizeof (error_code));
	    temp = (char*) temp + sizeof (error_code);

	    strcpy ((char*)temp, error_msg);
	    break;


	default:
	    printf ("unsupported opcode %d. \n", opcode);
	    break;
    }

out:
    return result;
}

/* get information from buf recieved from client.
 *  we are decoding RRQ and ACK only */
int decode (void* buf,
	short* p_opcode,
	short* p_block_num,	// for ACK onl, ignore otherwise
	char** p_file_name,	// for RRQ onl, ignore otherwise
	char** p_mode		// for RRQ onl, ignore otherwise
	)
{
    int result = 0;
    short opcode =  ntohs (*(short*) buf);
    char* temp_ptr = buf;

    /* second byte contains opcode */
    if (opcode == TFTP_OP_RRQ)
    {
	temp_ptr+= 2;
	/* now we have file_name */
	if (p_file_name)
	{
	    *p_file_name = malloc (strlen (temp_ptr) + 1);
	    memset (*p_file_name, 0, strlen (temp_ptr) + 1);

	    strcpy (*p_file_name, temp_ptr);

	    temp_ptr += strlen (temp_ptr) + 1;
	}
	else
	{
	    printf ("invalid pointer p_file_name. \n");
	    result = -1;
	    goto out;
	}

	if (p_mode)
	{
	    *p_mode = malloc (strlen (temp_ptr) + 1);
	    memset (*p_mode, 0, strlen (temp_ptr) + 1);
	    strcpy (*p_mode, temp_ptr);
	}
	else
	{
	    printf ("invalid pointer p_mode. \n");
	    result = -1;
	    goto out;
	}

	printf ("opcode: %x file_name: %s mode: %s\n",
		    opcode, *p_file_name, *p_mode);
    }
    else if (opcode == TFTP_OP_ACK)
    {
	temp_ptr+= 2;
	if (p_block_num)
	{
	    *p_block_num = ntohs (*( (short*) (temp_ptr)));
	    printf ("ack block num = %d.\n", *p_block_num);
	}
	else
	{
	    printf ("invalid pointer p_block_num. \n");
	    result = -1;
	    goto out;
	}

    }
    else
    {
	printf ("invalid packet. opcode = %d.\n", opcode);
	result = -1;
    }

    *p_opcode = opcode;
out:
    return result;
}

int tftp_is_valid_file_name (char* file_name, char** errbuf,
	int* p_len)
{
    FILE* file;

    if (strchr (file_name, 0x5C) || strchr (file_name, 0x2F))
	//look for illegal characters in the file_name string these are \ and /
    {
	printf ("bad file_name %s.\n", file_name);
	*p_len = sprintf (*errbuf,
		    "%c%c%c%cIllegal file_name.(%s) You may not attempt to \
		    descend or ascend directories.%c",
		    0x00, 0x05, 0x00, 0x00, file_name, 0x00);

	return 0;
    }

    if (!(file = fopen (file_name, "r")) )
    {
	printf ("could not open file %s.\n", file_name);

	/* TODO: send error packet*/
	*p_len = sprintf ((char *) *errbuf,
		"%c%c%c%cFile not found in cwd (%s)%c",
		0x00, 0x05, 0x00, 0x01, file_name, 0x00);
	return 0;
    }

    fclose (file);
    return 1;
}

int tftp_rrq_handler (
	struct sockaddr_in client_addr,
	socklen_t client_addr_len,
	tftp_client_t** p_client_list,
	int* p_current_client_count,
	void* buf,
	int num_bytes_rcvd)
{
    int out_buf_size = 0;
    int bytes_sent = 0;
    int sock = 0;
    int file_opened = 0;
    int result = 0;
    short opcode = 0;
    short block_num = 0;
    char* file_name = NULL;
    char file_data [TFTP_MAX_BUF_SIZE] = {0,};
    int size = 0;
    struct sockaddr_in new_addr;
    socklen_t	new_addr_len;
    int i = 0;
    int data_chunk_num = 1;
    int offset = 0;
    FILE* file;
    struct fpos_t* pos = NULL;
    char* mode_str = NULL;
    void* out_buf = NULL;
    tftp_client_t* client_list = *p_client_list;

    new_addr.sin_family = AF_INET;

//    printf ("inside rrq handler. \n");
    /* assign random port */
    new_addr.sin_port = 0;

    if (g_ip_addr)
	new_addr.sin_addr.s_addr = (g_ip_addr);
    else
	new_addr.sin_addr.s_addr = htonl (INADDR_ANY);

    if ((sock = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
    {
	printf ("could not create socket for responding.\n");
	result = -1;
	goto out;
    }

    result =  getsockname (sock, (struct sockaddr*) &new_addr, &new_addr_len);
#if 0
    if (result == -1)
    {
	printf ("could not getsockname. error = %s.\n",
		strerror (errno));
	result = errno;
	goto out;
    }
#endif
    result = bind (sock, (struct sockaddr *)&new_addr,
	    sizeof (new_addr));
    if (result == -1)
    {
	printf ("could not bind to the  socket. error = %s.\n",
		strerror (errno));
	result = errno;
	goto out;
    }
//    printf ("bound to ip = %s. port = %d \n", inet_ntoa (new_addr.sin_addr),
//	    ntohs (new_addr.sin_port));

    out_buf = malloc (TFTP_MAX_BUF_SIZE);

    result = decode (buf, &opcode, &block_num, &file_name, &mode_str);
    if (result == -1)
	goto out;

    if (opcode != TFTP_OP_RRQ)
    {
	printf ("error: recieved request %d frm a new client.\n", opcode);
	result = -1;
	goto out;
    }

#if 0
    if (!tftp_is_supported_mode (mode_str))
    {
	/* TODO: send error packet*/
	printf ("error: mode %s is not supported.\n", mode_str);
	result = -1;
	goto out;
    }
#endif

    if (!tftp_is_valid_file_name (file_name, (char**) (&out_buf),&out_buf_size))
    {
	if (sendto (sock, out_buf, TFTP_MAX_BUF_SIZE, 0,
		    (struct sockaddr *) &client_addr, client_addr_len)
		!= out_buf_size)
	{
	    printf ("sent bytes mismatch. error = %s.\n", strerror (errno));
	}
	printf ("error: file_name %s is not present in cwd.\n", file_name);
	goto out;
    }

    /* TODO: add socket in client_list with all the information here */
    if (*p_current_client_count < TFTP_MAX_CLIENT_COUNT)
    {
	for (i = 0; i < TFTP_MAX_CLIENT_COUNT; i++)
	{
	    if (!(client_list [i].socket))
	    {
		/* this spot is free, use this index i to add information about
		 * the new client here */
		break;
	    }
	}
    }
    else
    {
	printf ("can not service  client. max client limit.\n");
	goto out;
    }

    if(NULL == (file = fopen (file_name, "r")))
    {
	goto out;
    }

    file_opened = 1;
    if(!fseek(file, offset, SEEK_CUR));

    if(size = fread (file_data, sizeof(char), TFTP_FILE_BLOCK_SIZE, file))
    {
	if(0 != feof (file))
	{
	    char* eof_location = strchr (file_data, EOF);
	    if (eof_location != 0)
	    {
		size = eof_location - file_data + 1;
	    }
	}
	else if(ferror (file))
	{
	    printf ("error while reading from file %s. \n", file_name);
	}
    }

    result =  encode (TFTP_OP_DATA, data_chunk_num,
	    file_data, size, 0, NULL, &out_buf, &out_buf_size);
    if (result == -1)
	goto out;

    bytes_sent = sendto (sock, out_buf, out_buf_size, 0,
		(struct sockaddr *) &client_addr, client_addr_len);
    if (bytes_sent != out_buf_size)
    {
	goto out;
    }

    /* fill information for client for next file_chunk reads */
    memset ((void*) (&client_list [i]), 0, sizeof (tftp_client_t));

    client_list [i].socket = sock;
    client_list [i].current_retry_count = 0;
    client_list [i].current_retry_interval = TFTP_RETRY_INTERVAL;
    client_list [i].ack_num_expected = data_chunk_num;
    client_list [i].last_packet_sent_at = clock ();
    client_list [i].file_name = file_name;
    client_list [i]. current_offset += size;

    client_list [i].client = client_addr;
    client_list [i].client_len = client_addr_len;

    /* refill the buffer, it will be used if needed to resend the packet */
    if (client_list [i].buf)
	free (client_list [i].buf);

    client_list [i].buf = malloc (out_buf_size);
    memcpy (client_list [i].buf, out_buf, out_buf_size);

    /* add the new socket to fd */
    FD_SET (sock, &g_master_fds);
    if (sock > g_max_fd)
	g_max_fd = sock;


out:
    if (file_opened)
	fclose(file);
    return result;
}


int tftp_ack_handler (
	struct sockaddr_in client,
	socklen_t client_len,
	int socket,
	tftp_client_t** p_client_list,
	int* p_current_client_count,
	void* buf,
	int num_bytes_rcvd)
{
    void* out_buf = NULL;
    short ack_num;
    int out_buf_size = 0;
    int bytes_sent = 0;
    short opcode;
    int result = 0;
    FILE* file;
    int file_opened = 0;
    int i = 0;
    int size = 0;
    char file_data [TFTP_MAX_BUF_SIZE] = {0,};
    tftp_client_t* client_list = *p_client_list;
    short data_chunk_num = 0;

//    printf ("inside ack handler. \n");
    result = decode (buf, &opcode, &ack_num, NULL, NULL);
    if (result == -1)
	goto out;

    if (opcode != TFTP_OP_ACK)
    {
	printf ("error: recieved request %d frm an existing client.\n", opcode);
	result = -1;
	goto out;
    }

    for (i = 0; i < TFTP_MAX_CLIENT_COUNT; i++)
    {
	if ((client_list [i].socket) == socket)
	{
	    if ((unsigned)ack_num != (unsigned)client_list [i].ack_num_expected)
	    {
		printf ("ignoring ack %d. expected ack %d. \n",
		ack_num, client_list [i].ack_num_expected);
		goto out;
	    }

	    if (client_list [i].final_packet_sent)
		printf ("final ack recieved. closing socket. \n");

	    if(NULL == (file = fopen (client_list [i].file_name, "r")))
	    {
		printf ("file_read error for %s. \n", client_list [i].file_name);
		goto out;
	    }

	    file_opened = 1;
//	    if(!fsetpos (file, &(client_list [i].pos)));
	    if(!fseek(file, client_list [i].current_offset, SEEK_CUR));

	    if(size = fread (file_data, sizeof(char), TFTP_FILE_BLOCK_SIZE,
			file))
	    {
		if(0 != feof (file))
		{
		    char* eof_location = strchr (file_data, EOF);
		    if (eof_location != 0)
		    {
			size = eof_location - file_data + 1;
		    }
		}
		else if(ferror (file))
		{
		    printf ("error while reading from file %s. \n",
		    client_list [i].file_name);
		}
	    }

	    data_chunk_num = ack_num + 1;

	    result =  encode (TFTP_OP_DATA, data_chunk_num,
		    file_data, size, 0, NULL, &out_buf, &out_buf_size);
	    if (result == -1)
		goto out;

	    bytes_sent = sendto (socket, out_buf, out_buf_size,
		    0, (struct sockaddr *) &client, sizeof (client));
	    if (bytes_sent != out_buf_size)
	    {
		printf ("sent bytes mismatch. error = %s.\n", strerror (errno));
		goto out;
	    }

	    /* update values in client data structure */
	    if (size < TFTP_FILE_BLOCK_SIZE)
		client_list [i].final_packet_sent = 1;

	    client_list [i].ack_num_expected = data_chunk_num;
	    client_list [i].current_retry_count = 0;
	    client_list [i].current_retry_interval = TFTP_RETRY_INTERVAL;
	    client_list [i]. current_offset += size;
	    client_list [i]. last_packet_sent_at = clock ();

	    client_list [i].client = client;
	    client_list [i].client_len = client_len;

	    /* refill the buffer, it will be used if needed to resend the packet */
	    if (client_list [i].buf)
		free (client_list [i].buf);

	    client_list [i].buf = malloc (out_buf_size);
	    memcpy (client_list [i].buf, out_buf, out_buf_size);

	    break;
	}
    }

    if (i == TFTP_MAX_CLIENT_COUNT)
	printf ("error: did not find socket in our list. \n");
out:
    if (file_opened)
	fclose (file);

    return result;
}
