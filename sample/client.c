/*
 * Copyright (c) 2013-2015, University of Lugano
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the copyright holders nor the names of it
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <paxos.h>
#include <evpaxos.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <event2/event.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <ctype.h>
#define MAX_VALUE_SIZE 8192
#define CONSTANT_K 20


struct client_value
{
  struct timeval t;
  size_t size;
  char value[MAX_VALUE_SIZE];
};

struct stats
{
	int delivered;
	long min_latency;
	long max_latency;
	long avg_latency;
};

struct client
{
	int value_size;
	int outstanding;
	struct stats stats;
	struct event_base* base;
	struct bufferevent* bev;
	struct event* sig;
	struct evlearner* learner;
};

static void
handle_sigint(int sig, short ev, void* arg)
{
	struct event_base* base = arg;
	printf("Caught signal %d\n", sig);
	event_base_loopexit(base, NULL);
}

static void
client_submit_value(struct client* c)
{
	struct client_value* v;
	v = malloc(sizeof(struct client_value));
	gettimeofday(&v->t, NULL);
	v->size = c->value_size;
	bool flag = false;
	do 
	{
		printf("Which one do you want to input (read k) or (write k v) or (e) to exit: ");
		fgets (v->value, MAX_VALUE_SIZE, stdin);
		if ((strlen(v->value)>0) && (v->value[strlen (v->value) - 1] == '\n')) 
					v->value[strlen (v->value) - 1] = '\0';
		char* sentvalue;
		sentvalue = malloc(sizeof(char)*v->size);
		strcpy(sentvalue, v->value);
		
		char* tmp = strtok (v->value, " ");
		sentvalue[strlen(sentvalue)]='\0';
		bool isNumber = true;
		if (strcmp(tmp,"read")==0)
		{
			tmp = strtok(NULL, " ");
			//check data is integer
			for(int i = 0; i < strlen(tmp);i++)
			{ 
				if(!isdigit(tmp[i]))
				{
					isNumber = false;					
					break;
				}				
								
			}
			//if data is integer, do it
			if(isNumber == true)
			{
				int val = atoi(tmp);
				if (val >= 0 && val <= CONSTANT_K)
				{					
					printf("Submitting value: %s\n",sentvalue);
					size_t size = sizeof(struct timeval) + sizeof(size_t) + strlen(sentvalue);
					paxos_submit(c->bev, sentvalue, size);
					flag = true;
				}
			}			
			
		}
		else if(strcmp(tmp,"write")==0)
		{
			tmp = strtok(NULL, " ");
			//check data is integer
			for(int i = 0; i < strlen(tmp);i++)
			{ 
				if(!isdigit(tmp[i]))
				{
					isNumber = false;					
					break;
				}				
								
			}
			if (isNumber == true)
			{
				int val = atoi(tmp);
			
				if (val >= 0 && val <= CONSTANT_K)
				{
					tmp = strtok(NULL, " ");
					for(int i = 0; i < strlen(tmp);i++)
					{ 
						if(!isdigit(tmp[i]))
						{
							isNumber = false;					
							break;
						}				
								
					}
					if(isNumber == true)
					{	
								
						printf("submitting value: %s \n",sentvalue);
						size_t size = sizeof(struct timeval) + sizeof(size_t) + strlen(sentvalue);					
						paxos_submit(c->bev, sentvalue, size);
						flag = true;
					}
				}
				
									
			}
		}
		else if (strcmp(tmp,"e")==0)		
			exit(0);
			
		
	}while (flag == false);	

}

// Returns t2 - t1 in microseconds.
static long
timeval_diff(struct timeval* t1, struct timeval* t2)
{
	long us;
	us = (t2->tv_sec - t1->tv_sec) * 1e6;
	if (us < 0) return 0;
	us += (t2->tv_usec - t1->tv_usec);
	return us;
}
static void
update_stats(struct stats* stats, struct client_value* delivered)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	long lat = timeval_diff(&delivered->t, &tv);
	stats->delivered++;
	stats->avg_latency = stats->avg_latency + 
		((lat - stats->avg_latency) / stats->delivered);
	if (stats->min_latency == 0 || lat < stats->min_latency)
		stats->min_latency = lat;
	if (lat > stats->max_latency)
		stats->max_latency = lat;
}

static void
on_deliver(unsigned iid, char* value, size_t size, void* arg)
{
	struct client* c = arg;
	struct client_value* v = (struct client_value*)value;
	update_stats(&c->stats, v);
	client_submit_value(c);
}

static void
on_connect(struct bufferevent* bev, short events, void* arg)
{
	int i;
	struct client* c = arg;
	if (events & BEV_EVENT_CONNECTED) {
		printf("Connected to proposer\n");
		for (i = 0; i < c->outstanding; ++i)
			client_submit_value(c);
	} else {
		printf("%s\n", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
	}
}

static struct bufferevent* 
connect_to_proposer(struct client* c, const char* config, int proposer_id)
{
	struct bufferevent* bev;
	struct evpaxos_config* conf = evpaxos_config_read(config);
	if (conf == NULL) {
		printf("Failed to read config file %s\n", config);
		return NULL;
	}
	struct sockaddr_in addr = evpaxos_proposer_address(conf, proposer_id);
	bev = bufferevent_socket_new(c->base, -1, BEV_OPT_CLOSE_ON_FREE);
	bufferevent_setcb(bev, NULL, NULL, on_connect, c);
	bufferevent_enable(bev, EV_READ|EV_WRITE);
	bufferevent_socket_connect(bev, (struct sockaddr*)&addr, sizeof(addr));
	int flag = 1;
	setsockopt(bufferevent_getfd(bev), IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));
	return bev;
}

static struct client*
make_client(const char* config, int proposer_id, int outstanding, int value_size)
{
	struct client* c;
	c = malloc(sizeof(struct client));
	c->base = event_base_new();
	
	memset(&c->stats, 0, sizeof(struct stats));
	c->bev = connect_to_proposer(c, config, proposer_id);
	if (c->bev == NULL)
		exit(1);
	
	c->value_size = value_size;
	c->outstanding = outstanding;
	
	paxos_config.learner_catch_up = 0;
	c->learner = evlearner_init(config, on_deliver, c, c->base);
	
	c->sig = evsignal_new(c->base, SIGINT, handle_sigint, c->base);
	evsignal_add(c->sig, NULL);
	
	return c;
}

static void
client_free(struct client* c)
{
	bufferevent_free(c->bev);
	event_free(c->sig);
	event_base_free(c->base);
	if (c->learner)
		evlearner_free(c->learner);
	free(c);
}

static void
start_client(const char* config, int proposer_id, int outstanding, int value_size)
{
	struct client* client;
	client = make_client(config, proposer_id, outstanding, value_size);
	signal(SIGPIPE, SIG_IGN);
	event_base_dispatch(client->base);
	client_free(client);
}

static void
usage(const char* name)
{
	printf("Usage: %s [path/to/paxos.conf] [-h] [-o] [-v] [-p]\n", name);
	printf("  %-30s%s\n", "-h, --help", "Output this message and exit");
	printf("  %-30s%s\n", "-o, --outstanding #", "Number of outstanding client values");
	printf("  %-30s%s\n", "-v, --value-size #", "Size of client value (in bytes)");
	printf("  %-30s%s\n", "-p, --proposer-id #", "d of the proposer to connect to");
	exit(1);
}

int
main(int argc, char const *argv[])
{
	
	int i = 1;
	int proposer_id = 0;
	int outstanding = 1;
	int value_size = 64;
	const char* config = "../paxos.conf";

	if (argc > 1 && argv[1][0] != '-') {
		config = argv[1];
		i++;
	}

	while (i != argc) {
		
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
			usage(argv[0]);
		else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--outstanding") == 0)
			outstanding = atoi(argv[++i]);
		else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--value-size") == 0)
			value_size = atoi(argv[++i]);
		else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--proposer-id") == 0)
			proposer_id = atoi(argv[++i]);
		else
			usage(argv[0]);
		i++;
	}
	
	srand(time(NULL));

	start_client(config, proposer_id, outstanding, value_size);
	
	return 0;
}

