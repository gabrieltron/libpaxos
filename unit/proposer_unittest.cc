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


#include "proposer.h"
#include <gtest/gtest.h>

#define CHECK_ACCEPT(r, i, b, v, s) {   \
	ASSERT_EQ(r.iid, i);                \
	ASSERT_EQ(r.ballot, b);             \
	ASSERT_EQ(r.value.value_len, s);    \
	ASSERT_STREQ(r.value.value_val, v); \
}

class ProposerTest : public testing::Test {
protected:

	int quorum;
	struct proposer* p;
	static const int id = 2;
	static const int acceptors = 3;
	
	virtual void SetUp() {
		quorum = paxos_quorum(acceptors);
		paxos_config.proposer_timeout = 1;
		p = proposer_new(id, acceptors);
		paxos_config.verbosity = PAXOS_LOG_QUIET;
	}
	
	virtual void TearDown() {
		proposer_free(p);
	}
	
	void TestPrepareAckFromQuorum(iid_t iid, ballot_t bal) {
		paxos_prepare pr;
		for (size_t i = 0; i < quorum; ++i) {
			paxos_promise pa = (paxos_promise) {iid, bal, 0, 0};
			ASSERT_EQ(0, proposer_receive_promise(p, &pa, i, &pr));
		}
	}
	
	void TestPrepareAckFromQuorum(iid_t iid, ballot_t bal, 
		const char* value, ballot_t vbal = 0) {
		paxos_prepare pr;
		paxos_promise pa = (paxos_promise) {iid, bal, vbal,
			strlen(value)+1, (char*)value};
		for (size_t i = 0; i < quorum; ++i) {
			ASSERT_EQ(0, proposer_receive_promise(p, &pa, i, &pr));
		}
	}
	
	void TestAcceptAckFromQuorum(iid_t iid, ballot_t bal) {
		paxos_prepare pr;
		for (size_t i = 0; i < quorum; ++i) {
			paxos_accepted aa = (paxos_accepted) {iid, bal, bal, 0, 0};
			ASSERT_EQ(0, proposer_receive_accepted(p, &aa, i, &pr));
		}
	}
};

TEST_F(ProposerTest, Prepare) {
	int count = 10;
	paxos_prepare pr;
	for (int i = 0; i < count; ++i) {
		proposer_prepare(p, &pr);
		ASSERT_EQ(pr.iid, i+1);
		ASSERT_EQ(pr.ballot, id + MAX_N_OF_PROPOSERS);
	}
	ASSERT_EQ(count, proposer_prepared_count(p));
}

TEST_F(ProposerTest, IgnoreOldBallots) {
	paxos_prepare pr, preempted;
	paxos_promise pa;

	proposer_prepare(p, &pr);
	
	// ignore smaller ballot
	pa = (paxos_promise) {pr.iid, pr.ballot-1, 0, 0};
	ASSERT_EQ(0, proposer_receive_promise(p, &pa, 1, &preempted));
	
	// preempt
	pa = (paxos_promise) {pr.iid, pr.ballot+1, 0, 0};
	ASSERT_EQ(1, proposer_receive_promise(p, &pa, 1, &preempted));
	
	// again ignore smaller ballot
	pa = (paxos_promise) {pr.iid, pr.ballot, 0, 0};
	ASSERT_EQ(0, proposer_receive_promise(p, &pa, 1, &preempted));
}

TEST_F(ProposerTest, IgnoreDuplicatePrepareAcks) {
	paxos_prepare pr, preempted;
	paxos_accept ar;
	proposer_prepare(p, &pr);
	proposer_propose(p, "value", strlen("value")+1);
	for (size_t i = 0; i < 10; ++i) {
		paxos_promise pa = (paxos_promise) {pr.iid, pr.ballot, 0, 0};
		ASSERT_EQ(0, proposer_receive_promise(p, &pa, 2, &preempted));
		ASSERT_FALSE(proposer_accept(p, &ar));
	}
	paxos_promise pa = (paxos_promise) {pr.iid, pr.ballot, 0, 0};
	proposer_receive_promise(p, &pa, 1, &preempted);
	proposer_accept(p, &ar);
	TestAcceptAckFromQuorum(ar.iid, ar.ballot);
}

TEST_F(ProposerTest, PrepareAndAccept) {
	paxos_prepare pr;
	paxos_accept ar;
	char value[] = "a value";
	int value_size = strlen(value) + 1;	
	
	proposer_prepare(p, &pr);
	TestPrepareAckFromQuorum(pr.iid, pr.ballot);

	ASSERT_FALSE(proposer_accept(p, &ar)); // no value to propose yet
	
	proposer_propose(p, value, value_size);
	
	proposer_accept(p, &ar);
	CHECK_ACCEPT(ar, pr.iid, pr.ballot, value, value_size);
	
	TestAcceptAckFromQuorum(ar.iid, ar.ballot);
}

TEST_F(ProposerTest, PreparePreempted) {
	paxos_accept ar;
	paxos_prepare pr, preempted;
	char value[] = "some value";
	int value_size = strlen(value) + 1;
	
	proposer_prepare(p, &pr);
	proposer_propose(p, value, value_size);
	
	// preempt! proposer receives a different ballot...
	paxos_promise pa = (paxos_promise) {pr.iid, pr.ballot+1, 0, 0};
	ASSERT_EQ(1, proposer_receive_promise(p, &pa, 1, &preempted));
	ASSERT_EQ(preempted.iid, pr.iid);
	ASSERT_GT(preempted.ballot, pr.ballot);
	
	TestPrepareAckFromQuorum(preempted.iid, preempted.ballot);
	
	proposer_accept(p, &ar);
	CHECK_ACCEPT(ar, preempted.iid, preempted.ballot, value, value_size);
}

TEST_F(ProposerTest, PrepareAlreadyClosed) {
	paxos_accept ar;
	paxos_prepare pr, preempted;
	char value[] = "some value";
	int value_size = strlen(value) + 1;
	
	proposer_prepare(p, &pr);
	proposer_propose(p, value, value_size);

	// preempt! proposer receives a different ballot...
	paxos_promise pa = (paxos_promise) {pr.iid, pr.ballot+1, 0, 
		strlen("foo bar baz")+1, (char*)"foo bar baz"};
	
	ASSERT_EQ(1, proposer_receive_promise(p, &pa, 1, &preempted));
	ASSERT_EQ(preempted.iid, pr.iid);
	ASSERT_GT(preempted.ballot, pr.ballot);

	// acquire the instance
	TestPrepareAckFromQuorum(preempted.iid, preempted.ballot, value);

	// proposer has majority with the same value, 
	// we expect the instance to be closed
	proposer_accept(p, &ar);
	TestPrepareAckFromQuorum(ar.iid, ar.ballot);
	
	// try to accept our value on instance 2
	proposer_prepare(p, &pr);
	TestPrepareAckFromQuorum(pr.iid, pr.ballot);
	
	proposer_accept(p, &ar);
	CHECK_ACCEPT(ar, pr.iid, pr.ballot, value, value_size)
}

TEST_F(ProposerTest, PreparePreemptedWithTwoValues) {
	paxos_accept ar;
	paxos_prepare pr, preempted;

	proposer_prepare(p, &pr);
	proposer_propose(p, "v1", 3);
	
	// preempt with value
	paxos_promise pa1 = (paxos_promise) {pr.iid, pr.ballot+1, pr.ballot+1, 
		strlen("v2")+1, (char*)"v2" };
	paxos_promise pa2 = (paxos_promise) {pr.iid, pr.ballot+11, pr.ballot+11,
		strlen("v3")+1, (char*)"v3" };
	
	proposer_receive_promise(p, &pa1, 1, &preempted);
	proposer_receive_promise(p, &pa2, 2, &preempted);
	
	pa1.ballot = preempted.ballot;
	proposer_receive_promise(p, &pa1, 1, &preempted);
	
	ASSERT_FALSE(proposer_accept(p, &ar));
	
	pa2.ballot = preempted.ballot;
	proposer_receive_promise(p, &pa2, 2, &preempted);
	
	proposer_accept(p, &ar);
	CHECK_ACCEPT(ar, preempted.iid, preempted.ballot, "v3", 3);
	
	proposer_prepare(p, &pr);
	TestPrepareAckFromQuorum(pr.iid, pr.ballot);
	proposer_accept(p, &ar);
	CHECK_ACCEPT(ar, pr.iid, pr.ballot, "v1", 3);
}

TEST_F(ProposerTest, AcceptPreempted) {
	paxos_accept ar;
	paxos_prepare pr, preempted;
	char value[] = "some value";
	int value_size = strlen(value) + 1;
	
	proposer_prepare(p, &pr);
	proposer_propose(p, value, value_size);
	TestPrepareAckFromQuorum(pr.iid, pr.ballot);
	
	ASSERT_TRUE(proposer_accept(p, &ar));
	
	// preempt! proposer receives accept nack
	paxos_accepted aa = (paxos_accepted) {ar.iid, ar.ballot+1, ar.ballot+1, 0, 0};
	ASSERT_EQ(1, proposer_receive_accepted(p, &aa, 0, &preempted));
	ASSERT_EQ(preempted.iid, pr.iid);
	ASSERT_GT(preempted.ballot, ar.ballot);
	
	// check that proposer pushed the instance back to the prepare phase
	ASSERT_EQ(proposer_prepared_count(p), 1);
	
	// close the instance
	TestPrepareAckFromQuorum(preempted.iid, preempted.ballot, 
		"preempt", aa.value_ballot);
	proposer_accept(p, &ar);
	TestAcceptAckFromQuorum(preempted.iid, preempted.ballot);
	
	// make sure our value did not disappear...
	proposer_prepare(p, &pr);
	TestPrepareAckFromQuorum(pr.iid, pr.ballot);
	proposer_accept(p, &ar);
	CHECK_ACCEPT(ar, pr.iid, pr.ballot, value, value_size);
}

TEST_F(ProposerTest, PreparedCount) {
	int count = 100;
	paxos_accept ar;
	paxos_prepare pr, preempted;
	
	for (size_t i = 0; i < count; ++i) {
		proposer_prepare(p, &pr);
		proposer_propose(p, "some value", strlen("some value")+1);
		ASSERT_EQ(i + 1, proposer_prepared_count(p));
	}
	
	for (size_t i = 0; i < count; ++i)
		proposer_accept(p, &ar);
	ASSERT_EQ(count, proposer_prepared_count(p));
	
	for (size_t i = 0; i < count; ++i) {
		TestPrepareAckFromQuorum(i+1, pr.ballot);
		proposer_accept(p, &ar);
		ASSERT_EQ(count-(i+1), proposer_prepared_count(p));
	}
}

TEST_F(ProposerTest, PendingPrepareShouldTimeout) {
	paxos_prepare pr, to;
	struct timeout_iterator* iter;
	
	proposer_prepare(p, &pr);
	sleep(paxos_config.proposer_timeout);
	
	iter = proposer_timeout_iterator(p);
	ASSERT_TRUE(timeout_iterator_prepare(iter, &to));
	ASSERT_EQ(pr.iid, to.iid);
	ASSERT_EQ(pr.ballot, to.ballot);
	
	ASSERT_FALSE(timeout_iterator_prepare(iter, &to));
	timeout_iterator_free(iter);
}

TEST_F(ProposerTest, PreparedShouldNotTimeout) {
	struct timeout_iterator* iter;
	paxos_prepare pr1, pr2, preempted, to;

	proposer_prepare(p, &pr1);
	proposer_prepare(p, &pr2);
	TestPrepareAckFromQuorum(pr1.iid, pr1.ballot);
	
	sleep(paxos_config.proposer_timeout);
	
	iter = proposer_timeout_iterator(p);
	ASSERT_TRUE(timeout_iterator_prepare(iter, &to));
	ASSERT_EQ(pr2.iid, to.iid);
	ASSERT_EQ(pr2.ballot, to.ballot);
	
	ASSERT_FALSE(timeout_iterator_prepare(iter, &to));
	timeout_iterator_free(iter);
}

TEST_F(ProposerTest, PendingAcceptShouldTimeout) {
	paxos_prepare pr, preempted;
	paxos_accept ar, to;
	
	proposer_prepare(p, &pr);
	proposer_propose(p, "a value", strlen("a value")+1);
	TestPrepareAckFromQuorum(pr.iid, pr.ballot);

	proposer_accept(p, &ar);
	
	sleep(paxos_config.proposer_timeout);
	
	struct timeout_iterator* iter = proposer_timeout_iterator(p);
	ASSERT_TRUE(timeout_iterator_accept(iter, &to));
	ASSERT_EQ(pr.iid, to.iid);
	ASSERT_EQ(pr.ballot, to.ballot);
	
	ASSERT_FALSE(timeout_iterator_accept(iter, &to));
	timeout_iterator_free(iter);
}

TEST_F(ProposerTest, AcceptedShouldNotTimeout) {
	paxos_accept ar;
	paxos_prepare pr, preempted, to;

	// phase 1
	proposer_prepare(p, &pr);
	proposer_propose(p, "value", strlen("value")+1);
	TestPrepareAckFromQuorum(pr.iid, pr.ballot);
	
	// phase 2
	proposer_accept(p, &ar);
	TestAcceptAckFromQuorum(ar.iid, ar.ballot);
	
	// this one should timeout
	proposer_prepare(p, &pr);
	
	sleep(paxos_config.proposer_timeout);
	
	struct timeout_iterator* iter = proposer_timeout_iterator(p);
	ASSERT_TRUE(timeout_iterator_prepare(iter, &to));
	ASSERT_EQ(pr.iid, to.iid);
	ASSERT_EQ(pr.ballot, to.ballot);
	
	ASSERT_FALSE(timeout_iterator_prepare(iter, &to));
	ASSERT_FALSE(timeout_iterator_accept(iter, &ar));

	timeout_iterator_free(iter);
}

TEST_F(ProposerTest, ShouldNotTimeoutTwice) {
	paxos_prepare pr, to;
	struct timeout_iterator* iter;
	
	proposer_prepare(p, &pr);
	sleep(paxos_config.proposer_timeout);
	
	iter = proposer_timeout_iterator(p);
	ASSERT_TRUE(timeout_iterator_prepare(iter, &to));
	timeout_iterator_free(iter);
	
	iter = proposer_timeout_iterator(p);
	ASSERT_FALSE(timeout_iterator_prepare(iter, &to));
	timeout_iterator_free(iter);
}
