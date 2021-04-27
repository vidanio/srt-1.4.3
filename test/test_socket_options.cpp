/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2019 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Written by:
 *             Haivision Systems Inc.
 */

#include <gtest/gtest.h>
#include <future>
#include <thread>

// SRT includes
#include <socketconfig.h>
#include "srt.h"

using namespace std;


class TestSocketOptions
    : public ::testing::Test
{
protected:
    TestSocketOptions()
    {
        // initialization code here
    }

    ~TestSocketOptions()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

public:
    void StartListener()
    {
        // Specify address of the listener
        sockaddr* psa = (sockaddr*)&m_sa;
        ASSERT_NE(srt_bind(m_listen_sock, psa, sizeof m_sa), SRT_ERROR);

        srt_listen(m_listen_sock, 1);
    }

    SRTSOCKET EstablishConnection()
    {
        auto accept_async = [](SRTSOCKET listen_sock) {
            sockaddr_in client_address;
            int length = sizeof(sockaddr_in);
            const SRTSOCKET accepted_socket = srt_accept(listen_sock, (sockaddr*)&client_address, &length);
            return accepted_socket;
        };
        auto accept_res = async(launch::async, accept_async, m_listen_sock);

        sockaddr* psa = (sockaddr*)&m_sa;
        const int connect_res = srt_connect(m_caller_sock, psa, sizeof m_sa);
        EXPECT_EQ(connect_res, SRT_SUCCESS);

        const SRTSOCKET accepted_sock = accept_res.get();
        EXPECT_NE(accepted_sock, SRT_INVALID_SOCK);

        return accepted_sock;
    }

protected:
    // SetUp() is run immediately before a test starts.
    void SetUp()
    {
        ASSERT_GE(srt_startup(), 0);
        const int yes = 1;

        memset(&m_sa, 0, sizeof m_sa);
        m_sa.sin_family = AF_INET;
        m_sa.sin_port = htons(5200);
        ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &m_sa.sin_addr), 1);

        m_caller_sock = srt_create_socket();
        ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
        ASSERT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_RCVSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect
        ASSERT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_SNDSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect

        m_listen_sock = srt_create_socket();
        ASSERT_NE(m_listen_sock, SRT_INVALID_SOCK);
        ASSERT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_RCVSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect
        ASSERT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_SNDSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect
    }

    void TearDown()
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        ASSERT_NE(srt_close(m_caller_sock), SRT_ERROR);
        ASSERT_NE(srt_close(m_listen_sock), SRT_ERROR);
        srt_cleanup();
    }

protected:
    // put in any custom data members that you need

    sockaddr_in m_sa;
    SRTSOCKET m_caller_sock = SRT_INVALID_SOCK;
    SRTSOCKET m_listen_sock = SRT_INVALID_SOCK;

    int       m_pollid = 0;
};


/// A regression test for issue #735, fixed by PR #843.
/// Checks propagation of listener's socket option SRTO_LOSSMAXTTL
/// on SRT sockets being accepted.
TEST_F(TestSocketOptions, LossMaxTTL)
{
    const int loss_max_ttl = 5;
    ASSERT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_LOSSMAXTTL, &loss_max_ttl, sizeof loss_max_ttl), SRT_SUCCESS);

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    int opt_val = 0;
    int opt_len = 0;
    ASSERT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_LOSSMAXTTL, &opt_val, &opt_len), SRT_SUCCESS);
    EXPECT_EQ(opt_val, loss_max_ttl) << "Wrong SRTO_LOSSMAXTTL value on the accepted socket";
    EXPECT_EQ(opt_len, sizeof opt_len) << "Wrong SRTO_LOSSMAXTTL value length on the accepted socket";

    SRT_TRACEBSTATS stats;
    EXPECT_EQ(srt_bstats(accepted_sock, &stats, 0), SRT_SUCCESS);
    EXPECT_EQ(stats.pktReorderTolerance, loss_max_ttl);

    ASSERT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_LOSSMAXTTL, &opt_val, &opt_len), SRT_SUCCESS);
    EXPECT_EQ(opt_val, loss_max_ttl) << "Wrong SRTO_LOSSMAXTTL value on the listener socket";
    EXPECT_EQ(opt_len, sizeof opt_len) << "Wrong SRTO_LOSSMAXTTL value length on the listener socket";

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}


// Try to set/get SRTO_MININPUTBW with wrong optlen
TEST_F(TestSocketOptions, MinInputBWWrongLen)
{
    int64_t mininputbw = 0;
    int optlen = (int)(sizeof mininputbw) - 1;
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_MININPUTBW, &mininputbw, &optlen), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(NULL), SRT_EINVPARAM);
    optlen += 2;
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_MININPUTBW, &mininputbw, &optlen), SRT_SUCCESS) << "Bigger storage is allowed";
    EXPECT_EQ(optlen, (int)(sizeof mininputbw));

    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_MININPUTBW, &mininputbw, sizeof mininputbw - 1), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(NULL), SRT_EINVPARAM);
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_MININPUTBW, &mininputbw, sizeof mininputbw + 1), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(NULL), SRT_EINVPARAM);
}

// Check the default SRTO_MININPUTBW is SRT_PACING_MAXBW_DEFAULT
TEST_F(TestSocketOptions, MinInputBWDefault)
{
    const int mininputbw_expected = 0;
    int64_t mininputbw = 1;
    int optlen = (int)(sizeof mininputbw);
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_MININPUTBW, &mininputbw, &optlen), SRT_SUCCESS);
    EXPECT_EQ(optlen, (int)(sizeof mininputbw));
    EXPECT_EQ(mininputbw, mininputbw_expected);

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check both listener and accepted socket have default values
    for (SRTSOCKET sock : { m_listen_sock, accepted_sock })
    {
        optlen = (int)(sizeof mininputbw);
        EXPECT_EQ(srt_getsockopt(sock, 0, SRTO_MININPUTBW, &mininputbw, &optlen), SRT_SUCCESS);
        EXPECT_EQ(optlen, (int)(sizeof mininputbw));
        EXPECT_EQ(mininputbw, mininputbw_expected);
    }

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}

// Check setting and getting SRT_MININPUTBW
TEST_F(TestSocketOptions, MinInputBWSet)
{
    const int64_t mininputbw_dflt = 0;
    const int64_t mininputbw = 50000000;
    int optlen = (int)(sizeof mininputbw);

    int64_t bw = -100;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_MININPUTBW, &bw, sizeof bw), SRT_ERROR) << "Has to be a non-negative number";
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_MININPUTBW, &bw, &optlen), SRT_SUCCESS);
    EXPECT_EQ(bw, mininputbw_dflt);

    bw = mininputbw;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_MININPUTBW, &bw, sizeof bw), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_MININPUTBW, &bw, &optlen), SRT_SUCCESS);
    EXPECT_EQ(bw, mininputbw);

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check accepted socket inherits values
    for (SRTSOCKET sock : { m_listen_sock, accepted_sock })
    {
        optlen = (int)(sizeof bw);
        EXPECT_EQ(srt_getsockopt(sock, 0, SRTO_MININPUTBW, &bw, &optlen), SRT_SUCCESS);
        EXPECT_EQ(optlen, (int)(sizeof bw));
        EXPECT_EQ(bw, mininputbw);
    }

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}

// Check setting and getting SRTO_MININPUTBW in runtime
TEST_F(TestSocketOptions, MinInputBWRuntime)
{
    const int64_t mininputbw = 50000000;

    // Establish a connection
    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Test a connected socket
    int64_t bw = mininputbw;
    int optlen = (int)(sizeof bw);
    EXPECT_EQ(srt_setsockopt(accepted_sock, 0, SRTO_MININPUTBW, &bw, sizeof bw), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_MININPUTBW, &bw, &optlen), SRT_SUCCESS);
    EXPECT_EQ(bw, mininputbw);

    bw = 0;
    EXPECT_EQ(srt_setsockopt(accepted_sock, 0, SRTO_INPUTBW, &bw, sizeof bw), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_INPUTBW, &bw, &optlen), SRT_SUCCESS);
    EXPECT_EQ(bw, 0);

    EXPECT_EQ(srt_setsockopt(accepted_sock, 0, SRTO_MAXBW, &bw, sizeof bw), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_MAXBW, &bw, &optlen), SRT_SUCCESS);
    EXPECT_EQ(bw, 0);

    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_MININPUTBW, &bw, &optlen), SRT_SUCCESS);
    EXPECT_EQ(bw, mininputbw);

    const int64_t new_mininputbw = 20000000;
    bw = new_mininputbw;
    EXPECT_EQ(srt_setsockopt(accepted_sock, 0, SRTO_MININPUTBW, &bw, sizeof bw), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_MININPUTBW, &bw, &optlen), SRT_SUCCESS);
    EXPECT_EQ(bw, new_mininputbw);

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}

TEST_F(TestSocketOptions, StreamIDWrongLen)
{
    char buffer[CSrtConfig::MAX_SID_LENGTH + 135];
    for (size_t i = 0; i < sizeof buffer; ++i)
        buffer[i] = 'a' + i % 25;

    EXPECT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_STREAMID, buffer, CSrtConfig::MAX_SID_LENGTH+1), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(NULL), SRT_EINVPARAM);
}

// Try to set/get a 13-character string in SRTO_STREAMID.
// This tests checks that the StreamID is set to the correct size
// while it is transmitted as 16 characters in the Stream ID HS extension.
TEST_F(TestSocketOptions, StreamIDOdd)
{
    // 13 characters, that is, 3*4+1
    string sid_odd = "something1234";

    EXPECT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_STREAMID, sid_odd.c_str(), sid_odd.size()), SRT_SUCCESS);

    char buffer[CSrtConfig::MAX_SID_LENGTH + 135];
    int buffer_len = sizeof buffer;
    EXPECT_EQ(srt_getsockopt(m_caller_sock, 0, SRTO_STREAMID, &buffer, &buffer_len), SRT_SUCCESS);
    EXPECT_EQ(std::string(buffer), sid_odd);
    EXPECT_EQ(buffer_len, sid_odd.size());
    EXPECT_EQ(strlen(buffer), sid_odd.size());

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check accepted socket inherits values
    for (size_t i = 0; i < sizeof buffer; ++i)
        buffer[i] = 'a';
    buffer_len = (int)(sizeof buffer);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_STREAMID, &buffer, &buffer_len), SRT_SUCCESS);
    EXPECT_EQ(buffer_len, sid_odd.size());
    EXPECT_EQ(strlen(buffer), sid_odd.size());

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}


TEST_F(TestSocketOptions, StreamIDEven)
{
    // 12 characters = 4*3, that is, aligned to 4
    string sid_even = "123412341234";

    EXPECT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_STREAMID, sid_even.c_str(), sid_even.size()), SRT_SUCCESS);

    char buffer[CSrtConfig::MAX_SID_LENGTH + 135];
    int buffer_len = sizeof buffer;
    EXPECT_EQ(srt_getsockopt(m_caller_sock, 0, SRTO_STREAMID, &buffer, &buffer_len), SRT_SUCCESS);
    EXPECT_EQ(std::string(buffer), sid_even);
    EXPECT_EQ(buffer_len, sid_even.size());
    EXPECT_EQ(strlen(buffer), sid_even.size());

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check accepted socket inherits values
    for (size_t i = 0; i < sizeof buffer; ++i)
        buffer[i] = 'a';
    buffer_len = (int)(sizeof buffer);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_STREAMID, &buffer, &buffer_len), SRT_SUCCESS);
    EXPECT_EQ(buffer_len, sid_even.size());
    EXPECT_EQ(strlen(buffer), sid_even.size());

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}


TEST_F(TestSocketOptions, StreamIDAlmostFull)
{
    // 12 characters = 4*3, that is, aligned to 4
    string sid_amost_full;
    for (size_t i = 0; i < CSrtConfig::MAX_SID_LENGTH-2; ++i)
        sid_amost_full += 'x';

    // Just to manipulate the last ones.
    size_t size = sid_amost_full.size();
    sid_amost_full[size-2] = 'y';
    sid_amost_full[size-1] = 'z';

    EXPECT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_STREAMID, sid_amost_full.c_str(), sid_amost_full.size()), SRT_SUCCESS);

    char buffer[CSrtConfig::MAX_SID_LENGTH + 135];
    int buffer_len = sizeof buffer;
    EXPECT_EQ(srt_getsockopt(m_caller_sock, 0, SRTO_STREAMID, &buffer, &buffer_len), SRT_SUCCESS);
    EXPECT_EQ(std::string(buffer), sid_amost_full);
    EXPECT_EQ(buffer_len, sid_amost_full.size());
    EXPECT_EQ(strlen(buffer), sid_amost_full.size());

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check accepted socket inherits values
    for (size_t i = 0; i < sizeof buffer; ++i)
        buffer[i] = 'a';
    buffer_len = (int)(sizeof buffer);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_STREAMID, &buffer, &buffer_len), SRT_SUCCESS);
    EXPECT_EQ(buffer_len, sid_amost_full.size());
    EXPECT_EQ(strlen(buffer), sid_amost_full.size());
    EXPECT_EQ(buffer[sid_amost_full.size()-1], 'z');

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}

TEST_F(TestSocketOptions, StreamIDFull)
{
    // 12 characters = 4*3, that is, aligned to 4
    string sid_full;
    for (size_t i = 0; i < CSrtConfig::MAX_SID_LENGTH; ++i)
        sid_full += 'x';

    // Just to manipulate the last ones.
    size_t size = sid_full.size();
    sid_full[size-2] = 'y';
    sid_full[size-1] = 'z';

    EXPECT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_STREAMID, sid_full.c_str(), sid_full.size()), SRT_SUCCESS);

    char buffer[CSrtConfig::MAX_SID_LENGTH + 135];
    int buffer_len = sizeof buffer;
    EXPECT_EQ(srt_getsockopt(m_caller_sock, 0, SRTO_STREAMID, &buffer, &buffer_len), SRT_SUCCESS);
    EXPECT_EQ(std::string(buffer), sid_full);
    EXPECT_EQ(buffer_len, sid_full.size());
    EXPECT_EQ(strlen(buffer), sid_full.size());

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check accepted socket inherits values
    for (size_t i = 0; i < sizeof buffer; ++i)
        buffer[i] = 'a';
    buffer_len = (int)(sizeof buffer);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_STREAMID, &buffer, &buffer_len), SRT_SUCCESS);
    EXPECT_EQ(buffer_len, sid_full.size());
    EXPECT_EQ(strlen(buffer), sid_full.size());
    EXPECT_EQ(buffer[sid_full.size()-1], 'z');

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}

TEST_F(TestSocketOptions, StreamIDLenListener)
{
    string stream_id_13 = "something1234";

    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_STREAMID, stream_id_13.c_str(), stream_id_13.size()), SRT_SUCCESS);

    char buffer[648];
    int buffer_len = sizeof buffer;
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_STREAMID, &buffer, &buffer_len), SRT_SUCCESS);

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check accepted socket inherits values
    for (SRTSOCKET sock : { m_caller_sock, accepted_sock })
    {
        for (size_t i = 0; i < sizeof buffer; ++i)
            buffer[i] = 'a';
        buffer_len = (int)(sizeof buffer);
        EXPECT_EQ(srt_getsockopt(sock, 0, SRTO_STREAMID, &buffer, &buffer_len), SRT_SUCCESS);
        EXPECT_EQ(buffer_len, 0) << (sock == accepted_sock ? "ACCEPTED" : "LISTENER");
    }

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}
