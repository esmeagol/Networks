#include "tftp.h"

void usage (char **argv, int argc)
{
    printf ("%s ip port \n",argv [0]);
}

int serve_tftp_clients (int serv_sock)
{
    int			client_sock		= 0;
    int			port			= 0;
    int			result			= 0;
    int			recv_flags		= 0;
    int			current_client_count	= 0;
    int			num_bytes_rcvd		= 0;
    int			i, j			= 0;
    unsigned int	client_addr_len		= 0;
    struct		sockaddr_in	client_addr	= {0,};
    unsigned char	buf [TFTP_MAX_BUF_SIZE]		= {0,};
    struct		timeval	timeout		= {0,};
    tftp_client_t*	client_list		= NULL;
    int			wait_time		= 0;

    /* initialize socket desriptor lists */
    current_client_count = 0;
    FD_ZERO (&g_master_fds);
    FD_ZERO (&g_read_fds);
    FD_SET (serv_sock, &g_master_fds);
    g_max_fd = serv_sock;

    /* allocate space for client_list for keeping data per client */
    client_list = malloc (TFTP_MAX_CLIENT_COUNT * sizeof (tftp_client_t));
    if (!client_list)
    {
	printf ("could not allocate memory for client list. \n");
	result = -1;
	goto out;
    }

    memset (client_list, 0 , TFTP_MAX_CLIENT_COUNT * sizeof (tftp_client_t));

    /* timeout used for select == 500 msec. */
    timeout.tv_sec = 0;
    timeout.tv_usec = TFTP_SELECT_TIMEOUT;

    while (1)
    {
	for(i = 0; i < g_max_fd; i++)
	{
	    FD_SET (i, &g_master_fds);
	}

	FD_SET (serv_sock, &g_master_fds);
	g_read_fds = g_master_fds;

	if (select (g_max_fd + 1, &g_read_fds, NULL, NULL, &timeout) == -1)
	{
	    printf ("select failed. error = %s.\n",
		    strerror (errno));
	    result = errno;
	    goto out;
	}

	/* if retry_time has passed for any client, resend the packet here */
	for(i = 0; i <= g_max_fd; i++)
	{
	    g_current_time = clock();

	    for (j = 0; j < TFTP_MAX_CLIENT_COUNT; j++)
	    {
		if ((client_list [j].socket) == i)
		    break;
	    }

	    /* dont go further if any client not added. */
	    if (i == 0 || j == TFTP_MAX_CLIENT_COUNT)
		continue;

	    wait_time = ((g_current_time - client_list [j].last_packet_sent_at)/
		    (double) CLOCKS_PER_SEC);

	    if (wait_time >= TFTP_MAX_RETRY_TIME)
	    {
		if (client_list [j].buf)
		    free (client_list [j].buf);

		memset ((void*) (&client_list [j]), 0, sizeof (tftp_client_t));
		FD_CLR (i, &g_master_fds);
		close (i);
	    }

	    /* if client j has some retry interval and wait_time exceeds that,
	     * resend the packet
	     */

	    if (j && client_list [j].current_retry_interval &&
		    wait_time >= client_list [j].current_retry_interval &&
		    !client_list [j].final_packet_sent)
	    {
	//	printf ("wait time = %0.4lf seconds. \n", (double)wait_time);

		/* resend packet here */
		printf ("Resending packet %d to ip %s, port %d. \n",
			client_list [j].ack_num_expected,
			inet_ntoa (client_list [j].client.sin_addr),
			ntohs (client_list [j].client.sin_port));

		int bytes_sent = sendto (client_list [j].socket,
			client_list [j].buf,
			client_list [j].buf_size, 0,
			(struct sockaddr *) (&client_list [j].client),
			client_list [j].client_len);
		if (bytes_sent != client_list [j].buf_size)
		{
		    /* TODO: this is failing with INVALID_ARGUMENT,
		     * probably the client addr is not right here
		    */
		    printf ("sent bytes mismatch. error = %s.\n",
			    strerror (errno));
		}

		/* update information about try count and interval */
		client_list [j].current_retry_count ++;
		client_list [j].current_retry_interval *= 2;
		client_list [j]. last_packet_sent_at = clock ();
	    }
	}

	/* process all requests from clients here */
	for(i = 0; i <= g_max_fd; i++)
	{
	    if (FD_ISSET (i, &g_read_fds))
	    {
		memset (buf, 0 , TFTP_MAX_BUF_SIZE);
		num_bytes_rcvd = recvfrom (i, buf, TFTP_MAX_BUF_SIZE, 0,
			(struct sockaddr *) &client_addr,
			&client_addr_len);
		if (num_bytes_rcvd < 0)
		{
		    printf ("recvfrom failed. error = %s.\n",
			    strerror (errno));
		    result = errno;
		    goto out;
		}

		if (i == serv_sock)
		{
		    /* this should be an RRQ */
		    short opcode = ntohs (* (short*) buf);
		    if (opcode == TFTP_OP_RRQ)
		    {
			printf ("rrq received from ip = %s. port = %d.\n",
				inet_ntoa (client_addr.sin_addr),
				ntohs (client_addr.sin_port));

			result =  tftp_rrq_handler (client_addr,
				client_addr_len,
				&client_list,
				&current_client_count, buf,
				num_bytes_rcvd);
			if (result == -1)
			{
			    printf("After rrq handler.closing socket %d \n", i);
			    FD_CLR (i, &g_master_fds);
			    close (i);
			    break;
			}
		    }
		    else
		    {
			printf ("rrq not found. opcode = %d. \n", opcode);
		    }
		}
		else
		{
		    /* This should be an ACK */
		    short opcode =  ntohs (* (short*) buf);
		    short ack_num = ntohs (* ((short *) (buf+sizeof (short))));

		    if (opcode == TFTP_OP_ACK)
		    {
/*			printf ("ack %d received from ip = %s. port = %d.\n",
				ack_num, inet_ntoa (client_addr.sin_addr),
				ntohs (client_addr.sin_port));
*/
			result = tftp_ack_handler (client_addr, client_addr_len,
				i, &client_list,
				&current_client_count, buf, num_bytes_rcvd);
			if (result == -1)
			{
			    printf("After ack handler.closing socket %d \n", i);
			    /* cleanup */
			    for (j = 0; j < TFTP_MAX_CLIENT_COUNT; j++)
			    {
				if ((client_list [j].socket) == i)
				    break;
			    }
			    if (client_list [j].buf)
				free (client_list [j].buf);

			    memset ((void*) (&client_list [j]), 0,
				    sizeof (tftp_client_t));
			    FD_CLR (i, &g_master_fds);
			    close (i);
			    break;
			}
		    }
		    else
		    {
			printf ("ack not found. opcode = %d. \n", opcode);
		    }
		}
	    }
	}
    }
out:
    return result;
}

int main (int argc, char** argv)
{
	int             result          = 0;
	socklen_t       sock_addr_len   = sizeof (struct sockaddr);
	struct          sockaddr_in     serv_sock_addr    = {0,};
	struct          in_addr*        ip      = NULL;
	int		serv_sock;

	if (argc != 3)
	{
		usage (argv, argc);
		return (-1);
	}

	serv_sock_addr.sin_family = AF_INET;

	serv_sock_addr.sin_port = htons (atoi (argv [2]));

	/* take ip from user */
	result = inet_aton (argv [1], &(serv_sock_addr.sin_addr));
	if (result == 0)
	{
	    printf ("invalid server_ip %s. \n", argv [1]);
	    return (-1);
	}

	g_ip_addr = inet_addr (argv [1]);

	serv_sock = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if ( serv_sock == -1)
	{
		printf ("could not create socket. error = %s.\n",
				strerror (errno));
		result = errno;
		goto out;
	}

	result = bind (serv_sock, (const struct sockaddr *)&serv_sock_addr,
			sizeof (serv_sock_addr));
	if (result == -1)
	{
		printf ("could not bind to the  socket. error = %s.\n",
				strerror (errno));
		result = errno;
		goto out;
	}

	/* TODO: set socket options for port and address reuse.
	   setsockopt (serv_sock, SOLSOCKET, ADDRREUSE | PORTREUSE);
	 */
#if 0
	if (getsockname (serv_sock, &sock_addr, &sock_addr_len) == -1)
	{
		printf ("could not get socket info. error = %s.\n",
				strerror (errno));
		result = errno;
		goto out;
	}
#endif
	printf ("tftp server started on ip %s, port %d.\n",
			inet_ntoa (serv_sock_addr.sin_addr),
			ntohs (serv_sock_addr.sin_port));

	/* server is waiting for connections */
	printf ("Waiting for client connections... \n");

	result = serve_tftp_clients (serv_sock);
	if (result != 0)
	{
	    printf ("server closing with error %d. \n", result);
	}
out:
	if (serv_sock)
		close (serv_sock);

	return (result);
}
