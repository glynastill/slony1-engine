/*-------------------------------------------------------------------------
 * remote_listen.c
 *
 *	Implementation of the thread listening for events on
 *	a remote node database.
 *
 *	Copyright (c) 2003-2004, PostgreSQL Global Development Group
 *	Author: Jan Wieck, Afilias USA INC.
 *
 *	$Id: remote_listen.c,v 1.1 2004-02-22 03:10:48 wieck Exp $
 *-------------------------------------------------------------------------
 */


#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>

#include "libpq-fe.h"
#include "c.h"

#include "slon.h"


struct listat {
	int				li_origin;

	struct listat  *prev;
	struct listat  *next;
};


static void		remoteListen_adjust_listat(SlonNode *node,
							struct listat **listat_head, 
							struct listat **listat_tail);
static void		remoteListen_cleanup(struct listat **listat_head,
							struct listat **listat_tail);
static int		remoteListen_receive_events(SlonNode *node,
							SlonConn *conn, struct listat *listat);



/* ----------
 * slon_remoteListenThread
 *
 *	Listen for events on a remote database connection. This means,
 *	events generated by every other node we listen for on this one.
 * ----------
 */
void *
remoteListenThread_main(void *cdata)
{
	SlonNode   *node = (SlonNode *)cdata;
	SlonConn   *conn = NULL;
	char	   *conn_conninfo = NULL;
	char		conn_symname[64];
	int			rc;
	SlonDString	query1;
	PGconn	   *dbconn = NULL;
	PGresult   *res;
	PGnotify   *notification;

	struct listat  *listat_head;
	struct listat  *listat_tail;

printf("slon_remoteListenThread: node %d - start\n",
node->no_id);

	listat_head = NULL;
	listat_tail = NULL;
	dstring_init(&query1);

	sprintf(conn_symname, "node_%d_listen", node->no_id);

	while (true)
	{
		/*
		 * Lock the configuration and check if we are (still) supposed
		 * to exist.
		 */
		rtcfg_lock();

		/*
		 * If we have a database connection to the remote node, check
		 * if there was a change in the connection information.
		 */
		if (conn != NULL)
		{
			if (strcmp(conn_conninfo, node->pa_conninfo) != 0)
			{
printf("slon_remoteListenThread: node %d - disconnecting from '%s'\n",
node->no_id, conn_conninfo);
				slon_disconnectdb(conn);
				free(conn_conninfo);

				conn = NULL;
				conn_conninfo = NULL;
			}
		}

		/*
		 * Check our node's listen_status
		 */
		if (node->listen_status == SLON_TSTAT_NONE ||
			node->listen_status == SLON_TSTAT_SHUTDOWN)
		{
			rtcfg_unlock();
			break;
		}
		if (node->listen_status == SLON_TSTAT_RESTART)
			node->listen_status = SLON_TSTAT_RUNNING;

		/*
		 * Adjust the listat list and see if there is anything to
		 * listen for. If not, sleep for a while and check again,
		 * some node reconfiguration must be going on here.
		 */
		remoteListen_adjust_listat(node, &listat_head, &listat_tail);
		if (listat_head == NULL)
		{
			rtcfg_unlock();
printf("slon_remoteListenThread: node %d - nothing to listen for\n",
node->no_id);
			sched_msleep(node, 10000);
			continue;
		}

		/*
		 * Check if we have a database connection
		 */
		if (conn == NULL)
		{
			int		pa_connretry;

printf("slon_remoteListenThread: node %d - need DB connection\n",
node->no_id);
			/*
			 * Make sure we have connection info
			 */
			if (node->pa_conninfo == NULL)
			{
printf("slon_remoteListenThread: node %d - no conninfo - sleep 10 seconds\n",
node->no_id);

				rc = sched_msleep(node, 10000);
				if (rc != SCHED_STATUS_OK && rc != SCHED_STATUS_CANCEL)
					break;

				continue;
			}

			/*
			 * Try to establish a database connection to the remote
			 * node's database.
			 */
			conn_conninfo = strdup(node->pa_conninfo);
			pa_connretry = node->pa_connretry;
			rtcfg_unlock();

			conn = slon_connectdb(conn_conninfo, conn_symname);
			if (conn == NULL)
			{
				free(conn_conninfo);
				conn_conninfo = NULL;

printf("slon_remoteListenThread: node %d - DB connection failed - sleep %d seconds\n",
node->no_id, pa_connretry);

				rc = sched_msleep(node, pa_connretry * 1000);
				if (rc != SCHED_STATUS_OK && rc != SCHED_STATUS_CANCEL)
					break;

				continue;
			}
			dbconn = conn->dbconn;

			/*
			 * Listen on the connection for events and confirmations
			 */
			slon_mkquery(&query1,
					"listen \"_%s_Event\"; "
					"listen \"_%s_Confirm\"; ",
					rtcfg_cluster_name, rtcfg_cluster_name);
			res = PQexec(dbconn, dstring_data(&query1));
			if (PQresultStatus(res) != PGRES_COMMAND_OK)
			{
				fprintf(stderr, "slon_remoteListenThread: node %d - \"%s\" - %s",
						node->no_id,
						dstring_data(&query1), PQresultErrorMessage(res));
				PQclear(res);
				slon_disconnectdb(conn);
				free(conn_conninfo);

				rc = sched_msleep(node, pa_connretry * 1000);
				if (rc != SCHED_STATUS_OK && rc != SCHED_STATUS_CANCEL)
					break;

				continue;
			}

printf("slon_remoteListenThread: node %d - have DB connection to '%s'\n",
node->no_id, conn_conninfo);
		}
		else
			rtcfg_unlock();

		/*
		 * Receive events from the provider node
		 */
		rc = remoteListen_receive_events(node, conn, listat_head);
		if (rc < 0)
		{
			slon_disconnectdb(conn);
			free(conn_conninfo);
			conn = NULL;
			conn_conninfo = NULL;

			continue;
		}

printf("slon_remoteListenThread: node %d - waiting for action\n",
node->no_id);
		/*
		 * Wait for notification.
		 */
		rc = sched_wait_conn(conn, SCHED_WAIT_SOCK_READ);
		if (rc == SCHED_STATUS_CANCEL)
			continue;
		if (rc != SCHED_STATUS_OK)
			break;

printf("slon_remoteListenThread: node %d - ACTION\n",
node->no_id);
		PQconsumeInput(dbconn);
		while ((notification = PQnotifies(dbconn)) != NULL)
		{
printf("slon_remoteListenThread: node %d - notify '%s' received\n",
node->no_id, notification->relname);
			PQfreemem(notification);
		}
	}

printf("slon_remoteListenThread: node %d - exiting\n",
node->no_id);
	if (conn != NULL)
	{
		slon_disconnectdb(conn);
		free(conn_conninfo);
		conn = NULL;
		conn_conninfo = NULL;
	}

	remoteListen_cleanup(&listat_head, &listat_tail);

	rtcfg_lock();
	node->listen_status = SLON_TSTAT_NONE;
	rtcfg_unlock();

printf("slon_remoteListenThread: node %d - done\n",
node->no_id);
	pthread_exit(NULL);
}


static void
remoteListen_adjust_listat(SlonNode *node, struct listat **listat_head, 
			struct listat **listat_tail)
{
	SlonNode	   *lnode;
	SlonListen	   *listen;
	struct listat  *listat;
	struct listat  *linext;
	int				found;

	/*
	 * Remove listat entries for event origins that this remote node
	 * stopped providing for us, or where the origin got disabled.
	 */
	for (listat = *listat_head; listat;)
	{
		linext = listat->next;

		found = false;
		for (listen = node->listen_head; listen; listen = listen->next)
		{
			/*
			 * Check if the sl_listen entry still exists and that
			 * the li_origin is active.
			 */
			if (listen->li_origin == listat->li_origin)
			{
				lnode = rtcfg_findNode(listat->li_origin);
				if (lnode != NULL && lnode->no_active)
					found = true;
				break;
			}

			/* 
		 	 * Remove obsolete item
			 */
			if (!found)
			{
printf("remoteListenThread: node %d - remove listen status for event origin %d\n",
node->no_id, listat->li_origin);
				DLLIST_REMOVE(*listat_head, *listat_tail, listat);
				free(listat);
			}
		}

		listat = linext;
	}

	/*
	 * Now add new or newly enabled sl_listen entries to it.
	 */
	for (listen = node->listen_head; listen; listen = listen->next)
	{
		/*
		 * skip inactive or unknown nodes
		 */
		lnode = rtcfg_findNode(listen->li_origin);
		if (lnode == NULL || !(lnode->no_active))
			continue;

		/*
		 * check if we have that entry
		 */
		found = false;
		for (listat = *listat_head; listat; listat = listat->next)
		{
			if (listen->li_origin == listat->li_origin)
			{
				found = true;
				break;
			}
		}

		/*
		 * Add missing entries
		 */
		if (!found)
		{
printf("remoteListenThread: node %d - add listen status for event origin %d\n",
node->no_id, listen->li_origin);
			listat = (struct listat *)malloc(sizeof(struct listat));
			memset(listat, 0, sizeof(struct listat));

			listat->li_origin = listen->li_origin;
			DLLIST_ADD_TAIL(*listat_head, *listat_tail, listat);
		}
	}
}


static void
remoteListen_cleanup(struct listat **listat_head, struct listat **listat_tail)
{
	struct listat  *listat;
	struct listat  *linext;

	for (listat = *listat_head; listat;)
	{
		linext = listat->next;
		DLLIST_REMOVE(*listat_head, *listat_tail, listat);
		free(listat);

		listat = linext;
	}
}


static int
remoteListen_receive_events(SlonNode *node, SlonConn *conn,
			struct listat *listat)
{
	SlonNode	   *origin;
	SlonDString		query;
	char		   *where_or_and;
	char			seqno_buf[64];
	PGresult	   *res;
	int				ntuples;
	int				tupno;

	dstring_init(&query);

	slon_mkquery(&query,
			"select ev_origin, ev_seqno, ev_timestamp, "
			"       ev_minxid, ev_maxxid, ev_xip, "
			"       ev_type, "
			"       ev_data1, ev_data2, "
			"       ev_data3, ev_data4, "
			"       ev_data5, ev_data6, "
			"       ev_data7, ev_data8 "
			"from %s.sl_event e",
			rtcfg_namespace);

	rtcfg_lock();

	where_or_and = "where";
	while (listat)
	{
		if ((origin = rtcfg_findNode(listat->li_origin)) == NULL)
		{
			rtcfg_unlock();
			fprintf(stderr, "remoteListen_receive_events(): node %d not found\n",
					listat->li_origin);
			dstring_free(&query);
			return -1;
		}

		sprintf(seqno_buf, "%lld", origin->last_event);
		slon_appendquery(&query,
				" %s (e.ev_origin = '%d' and e.ev_seqno > '%s')",
				where_or_and, listat->li_origin, seqno_buf);

		where_or_and = "and";
		listat = listat->next;
	}
	slon_appendquery(&query, " order by e.ev_origin, e.ev_seqno");

	rtcfg_unlock();

	res = PQexec(conn->dbconn, dstring_data(&query));
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		fprintf(stderr, "remoteListen_receive_events(): node %d \"%s\" - %s",
				listat->li_origin,
				dstring_data(&query), PQresultErrorMessage(res));
		PQclear(res);
		dstring_free(&query);
		return -1;
	}
	dstring_free(&query);

	ntuples = PQntuples(res);
	for (tupno = 0; tupno < ntuples; tupno++)
	{
		int			ev_origin;
		int64		ev_seqno;
		char	   *ev_type;
		int64		retseq;

		ev_origin = strtol(PQgetvalue(res, tupno, 0), NULL, 10);
		sscanf(PQgetvalue(res, tupno, 1), "%lld", &ev_seqno);
		ev_type = PQgetvalue(res, tupno, 6);

printf("remoteListen_receive_events(): node %d - ev_origin %d ev_seqno %lld %s\n",
node->no_id, ev_origin, ev_seqno, ev_type);

		retseq = rtcfg_setNodeLastEvent(ev_origin, ev_seqno);
		if (retseq <= 0)
		{
printf("remoteListen_receive_events(): node %d - ev_origin %d ev_seqno %lld already known\n",
node->no_id, ev_origin, ev_seqno);
			continue;
		}
printf("TODO remoteListen_receive_events(): node %d - Queue event!\n",
node->no_id);
	}

	PQclear(res);

	return 0;
}


