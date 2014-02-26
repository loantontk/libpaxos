/*
	Copyright (c) 2013, University of Lugano
	All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:
		* Redistributions of source code must retain the above copyright
		  notice, this list of conditions and the following disclaimer.
		* Redistributions in binary form must reproduce the above copyright
		  notice, this list of conditions and the following disclaimer in the
		  documentation and/or other materials provided with the distribution.
		* Neither the name of the copyright holders nor the
		  names of its contributors may be used to endorse or promote products
		  derived from this software without specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
	ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
	DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
	(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
	LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
	ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
	(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF 
	THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.	
*/


#include "evpaxos.h"
#include "peers.h"
#include "config.h"
#include "acceptor.h"
#include "message.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <event2/event.h>

struct evacceptor
{
	struct peers* peers;
	struct acceptor* state;
};


static void
peer_send_accepted(struct peer* p, void* arg)
{
	send_paxos_accepted(peer_get_buffer(p), arg);
}

/*
	Received a prepare request (phase 1a).
*/
static void 
evacceptor_handle_prepare(struct peer* p, paxos_message* msg, void* arg)
{
	paxos_promise promise;
	paxos_prepare* prepare = &msg->u.prepare;
	struct evacceptor* a = (struct evacceptor*)arg;
	paxos_log_debug("Handle prepare for iid %d ballot %d",
		prepare->iid, prepare->ballot);
	acceptor_receive_prepare(a->state, prepare, &promise);
	send_paxos_promise(peer_get_buffer(p), &promise);
	paxos_promise_destroy(&promise);
}

/*
	Received a accept request (phase 2a).
*/
static void 
evacceptor_handle_accept(struct peer* p, paxos_message* msg, void* arg)
{	
	paxos_accepted accepted;
	paxos_accept* accept = &msg->u.accept;
	struct evacceptor* a = (struct evacceptor*)arg;
	paxos_log_debug("Handle accept for iid %d bal %d", 
		accept->iid, accept->ballot);
	int has_accepted = acceptor_receive_accept(a->state, accept, &accepted);
	if (has_accepted) {
		peers_foreach_client(a->peers, peer_send_accepted, &accepted);
		paxos_accepted_destroy(&accepted);
	} else {
		paxos_preempted preempted = {accepted.iid, accepted.ballot};
		send_paxos_preempted(peer_get_buffer(p), &preempted);
	}
}

static void
evacceptor_handle_repeat(struct peer* p, paxos_message* msg, void* arg)
{
	iid_t iid;
	paxos_accepted accepted;
	paxos_repeat* repeat = &msg->u.repeat;
	struct evacceptor* a = (struct evacceptor*)arg;
	paxos_log_debug("Handle repeat for iids %d-%d", repeat->from, repeat->to);
	for (iid = repeat->from; iid <= repeat->to; ++iid) {
		if (acceptor_receive_repeat(a->state, iid, &accepted)) {
			send_paxos_accepted(peer_get_buffer(p), &accepted);
			paxos_accepted_destroy(&accepted);
		}
	}
}

struct evacceptor*
evacceptor_init_internal(int id, struct evpaxos_config* c, struct peers* p)
{
	struct evacceptor* acceptor;
	
	acceptor = calloc(1, sizeof(struct evacceptor));
	acceptor->state = acceptor_new(id);
	acceptor->peers = p;
	
	peers_subscribe(p, PAXOS_PREPARE, evacceptor_handle_prepare, acceptor);
	peers_subscribe(p, PAXOS_ACCEPT, evacceptor_handle_accept, acceptor);
	peers_subscribe(p, PAXOS_REPEAT, evacceptor_handle_repeat, acceptor);

	return acceptor;
}

struct evacceptor*
evacceptor_init(int id, const char* config_file, struct event_base* base)
{
	struct evpaxos_config* config = evpaxos_config_read(config_file);
	if (config  == NULL)
		return NULL;
	
	int acceptor_count = evpaxos_acceptor_count(config);
	if (id < 0 || id >= acceptor_count) {
		paxos_log_error("Invalid acceptor id: %d.", id);
		paxos_log_error("Should be between 0 and %d", acceptor_count);
		evpaxos_config_free(config);
		return NULL;
	}

	struct peers* peers = peers_new(base, config);
	int port = evpaxos_acceptor_listen_port(config, id);
	if (peers_listen(peers, port) == 0)
		return NULL;
	struct evacceptor* acceptor = evacceptor_init_internal(id, config, peers);
	evpaxos_config_free(config);
	return acceptor;
}

void
evacceptor_free_internal(struct evacceptor* a)
{
	acceptor_free(a->state);
	free(a);
}

void
evacceptor_free(struct evacceptor* a)
{
	peers_free(a->peers);
	evacceptor_free_internal(a);
}
