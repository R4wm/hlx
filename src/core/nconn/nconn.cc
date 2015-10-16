//: ----------------------------------------------------------------------------
//: Copyright (C) 2014 Verizon.  All Rights Reserved.
//: All Rights Reserved
//:
//: \file:    nconn.cc
//: \details: TODO
//: \author:  Reed P. Morrison
//: \date:    02/07/2014
//:
//:   Licensed under the Apache License, Version 2.0 (the "License");
//:   you may not use this file except in compliance with the License.
//:   You may obtain a copy of the License at
//:
//:       http://www.apache.org/licenses/LICENSE-2.0
//:
//:   Unless required by applicable law or agreed to in writing, software
//:   distributed under the License is distributed on an "AS IS" BASIS,
//:   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//:   See the License for the specific language governing permissions and
//:   limitations under the License.
//:
//: ----------------------------------------------------------------------------

//: ----------------------------------------------------------------------------
//: Includes
//: ----------------------------------------------------------------------------
#include "nconn.h"
#include "nbq.h"
#include "evr.h"
#include "time_util.h"
#include "http_cb.h"

#include <errno.h>
#include <string.h>


namespace ns_hlx {

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
int32_t nconn::nc_run_state_machine(evr_loop *a_evr_loop, mode_t a_mode)
{
        //NDBG_PRINT("%sRUN_STATE_MACHINE%s: CONN[%p] STATE[%d] MODE: %d --START\n",
        //                ANSI_COLOR_BG_YELLOW, ANSI_COLOR_OFF, this, m_nc_state, a_mode);
state_top:
        //NDBG_PRINT("%sRUN_STATE_MACHINE%s: CONN[%p] STATE[%d] MODE: %d\n",
        //                ANSI_COLOR_FG_YELLOW, ANSI_COLOR_OFF, this, m_nc_state, a_mode);
        switch (m_nc_state)
        {

        // -------------------------------------------------
        // STATE: FREE
        // -------------------------------------------------
        case NC_STATE_FREE:
        {
                int32_t l_status;
                l_status = ncsetup(a_evr_loop);
                if(l_status != NC_STATUS_OK)
                {
                        NDBG_PRINT("Error performing ncsetup\n");
                        return STATUS_ERROR;
                }

                // TODO -check for errors
                m_nc_state = NC_STATE_CONNECTING;

                // Stats
                if(m_collect_stats_flag)
                {
                        m_connect_start_time_us = get_time_us();
                }

                goto state_top;
        }

        // -------------------------------------------------
        // STATE: LISTENING
        // -------------------------------------------------
        case NC_STATE_LISTENING:
        {
                int32_t l_status;
                l_status = ncaccept(a_evr_loop);
                if(l_status < 0)
                {
                        //NDBG_PRINT("Error performing ncaccept\n");
                        return STATUS_ERROR;
                }
                //NDBG_PRINT("%sRUN_STATE_MACHINE%s: ACCEPT[%d]\n", ANSI_COLOR_BG_RED, ANSI_COLOR_OFF, l_status);
                // Returning client fd
                return l_status;
        }

        // -------------------------------------------------
        // STATE: CONNECTING
        // -------------------------------------------------
        case NC_STATE_CONNECTING:
        {
                int32_t l_status;
                //NDBG_PRINT("connect...\n");
                l_status = ncconnect(a_evr_loop);
                if(l_status == NC_STATUS_ERROR)
                {
                        //NDBG_PRINT("Error performing ncconnect for host: %s.\n", m_host.c_str());
                        return STATUS_ERROR;
                }
                if(is_connecting())
                {
                        //NDBG_PRINT("Still connecting...\n");
                        return NC_STATUS_AGAIN;
                }
                //NDBG_PRINT("Connected: %s\n", m_host.c_str());
                // Returning client fd
                // If OK -change state to connected???
                m_nc_state = NC_STATE_CONNECTED;
                if(m_collect_stats_flag)
                {
                        m_stat.m_tt_connect_us = get_delta_time_us(m_connect_start_time_us);
                }
                if(m_connect_only)
                {
                        // TODO -this aint cool...
                        // Set state to 200
                        http_resp *l_rx = &((static_cast<http_data_t *>(m_data))->m_http_resp);
                        if(l_rx)
                        {
                                l_rx->set_status(200);
                        }
                        m_nc_state = NC_STATE_DONE;
                }
                goto state_top;
        }

        // -------------------------------------------------
        // STATE: ACCEPTING
        // -------------------------------------------------
        case NC_STATE_ACCEPTING:
        {
                int32_t l_status;
                l_status = ncaccept(a_evr_loop);
                if(l_status == NC_STATUS_ERROR)
                {
                        //NDBG_PRINT("Error performing ncaccept\n");
                        return STATUS_ERROR;
                }
                if(is_accepting())
                {
                        //NDBG_PRINT("Still connecting...\n");
                        return STATUS_OK;
                }
                m_nc_state = NC_STATE_CONNECTED;
                goto state_top;
        }

        // -------------------------------------------------
        // STATE: CONNECTED
        // -------------------------------------------------
        case NC_STATE_CONNECTED:
        {
                int32_t l_status = STATUS_OK;
                int32_t l_bytes = 0;
                switch(a_mode)
                {
                case NC_MODE_READ:
                {
                        l_status = nc_read();
                        //NDBG_PRINT("l_status: %d\n", l_status);
                        if(l_status == NC_STATUS_ERROR)
                        {
                                //NDBG_PRINT("Error performing nc_read -host: %s\n", m_host.c_str());
                                return STATUS_ERROR;
                        }
                        else if(l_status == NC_STATUS_EOF)
                        {
                                //NDBG_PRINT("NC_STATUS_EOF\n");
                                return NC_STATUS_EOF;
                        }
                        else if(l_status == NC_STATUS_AGAIN)
                        {
                                //NDBG_PRINT("NC_STATUS_EOF\n");
                                return NC_STATUS_AGAIN;
                        }
                        // TODO other states???
                        if(l_status > 0)
                        {
                                l_bytes += l_status;
                                if(m_collect_stats_flag)
                                {
                                        m_stat.m_total_bytes += l_status;
                                        if(m_stat.m_tt_first_read_us == 0)
                                        {
                                                m_stat.m_tt_first_read_us = get_delta_time_us(m_request_start_time_us);
                                        }
                                }
                        }
                        break;
                }
                case NC_MODE_WRITE:
                {
                        l_status = nc_write();
                        if(l_status == NC_STATUS_ERROR)
                        {
                                //NDBG_PRINT("Error performing nc_write\n");
                                return STATUS_ERROR;
                        }
                        else if(l_status == NC_STATUS_EOF)
                        {
                                //NDBG_PRINT("NC_STATUS_EOF\n");
                                return NC_STATUS_EOF;
                        }
                        else if(l_status == NC_STATUS_AGAIN)
                        {
                                //NDBG_PRINT("NC_STATUS_AGAIN\n");
                                return NC_STATUS_AGAIN;
                        }
                        if(l_status > 0)
                        {
                                l_bytes += l_status;
                        }
                        // TODO -if EAGAIN -mod evr for EPOLL_OUT

                        break;
                }
                default:
                {
                        break;
                }
                }
                return l_bytes;
        }
        // -------------------------------------------------
        // STATE: DONE
        // -------------------------------------------------
        case NC_STATE_DONE:
        {
                // nothing???
                break;
        }
        default:
        {
                break;
        }
        }
        return STATUS_OK;
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
int32_t nconn::nc_read(void)
{
        //NDBG_PRINT("%sTRY_READ%s: \n", ANSI_COLOR_BG_RED, ANSI_COLOR_OFF);
        if(!m_in_q)
        {
                NDBG_PRINT("Error m_in_q == NULL\n");
                return NC_STATUS_ERROR;
        }
        // -------------------------------------------------
        // while connection is readable...
        //   read up to next read size
        //   if size read == read_q free size
        //     add block to queue
        // -------------------------------------------------
        int32_t l_bytes_read = 0;
        int32_t l_total_read = 0;
        do {
                if(m_in_q->b_write_avail() <= 0)
                {
                        int32_t l_status = m_in_q->b_write_add_avail();
                        if(l_status <= 0)
                        {
                                NDBG_PRINT("Error performing b_write_add_avail\n");
                                return NC_STATUS_ERROR;
                        }
                }
                uint32_t l_read_size = m_in_q->b_write_avail();
                //NDBG_PRINT("%sTRY_READ%s: l_read_size: %d\n", ANSI_COLOR_BG_RED, ANSI_COLOR_OFF, l_read_size);
                char *l_buf = m_in_q->b_write_ptr();
                //NDBG_PRINT("%sTRY_READ%s: m_out_q->read_ptr(): %p m_out_q->read_avail(): %d\n",
                //                ANSI_COLOR_FG_RED, ANSI_COLOR_OFF,
                //                l_buf,
                //                l_read_size);
                l_bytes_read = ncread(l_buf, l_read_size);
                //NDBG_PRINT("%sTRY_READ%s: l_bytes_read: %d\n", ANSI_COLOR_FG_RED, ANSI_COLOR_OFF, l_bytes_read);
                switch(l_bytes_read){
                case NC_STATUS_ERROR:
                {
                        //NDBG_PRINT("Error performing ncread: status: %d\n", l_bytes_read);
                        return NC_STATUS_ERROR;
                }
                case NC_STATUS_AGAIN:
                {
                        return NC_STATUS_AGAIN;
                }
                case NC_STATUS_OK:
                {
                        return NC_STATUS_OK;
                }
                case NC_STATUS_EOF:
                {
                        return NC_STATUS_EOF;
                }
                default:
                {
                        break;
                }
                }
                //NDBG_PRINT("%sTRY_READ%s: l_bytes_read: %d old_size: %d-error:%d: %s\n",
                //                ANSI_COLOR_FG_RED, ANSI_COLOR_OFF,
                //                l_bytes_read,
                //                m_in_q->read_avail(),
                //                errno, strerror(errno));
                if(l_bytes_read > 0)
                {
                        l_total_read += l_bytes_read;
                        //ns_hlx::mem_display((uint8_t *)(l_buf), l_bytes_read);
                        m_in_q->b_write_incr(l_bytes_read);
                        if(m_read_cb)
                        {
                                int32_t l_status = m_read_cb(m_data, l_buf, l_bytes_read);
                                if(l_status != STATUS_OK)
                                {
                                        NDBG_PRINT("Error performing m_read_cb\n");
                                        return NC_STATUS_ERROR;
                                }
                        }
                }
                //???
                if((uint32_t)l_bytes_read < l_read_size)
                {
                        // Read as much as can -done...
                        break;
                }
        } while(l_bytes_read > 0);
        return l_total_read;
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
int32_t nconn::nc_write(void)
{
        //NDBG_PRINT("%sTRY_WRITE%s: m_out_q: %p\n", ANSI_COLOR_BG_GREEN, ANSI_COLOR_OFF, m_out_q);
        if(!m_out_q)
        {
                NDBG_PRINT("Error m_out_q == NULL\n");
                return NC_STATUS_ERROR;
        }

        if(!m_out_q->read_avail())
        {
                //NDBG_PRINT("Error l_write_size == %d\n", m_out_q->read_avail());
                return 0;
        }
        //NDBG_PRINT("%sTRY_WRITE%s: l_write_size: %u\n", ANSI_COLOR_BG_GREEN, ANSI_COLOR_OFF, m_out_q->read_avail());
        // -------------------------------------------------
        // while connection is writeable...
        //   wrtie up to next write size
        //   if size write == write_q free size
        //     add
        // -------------------------------------------------
        int32_t l_bytes_written;
        do {
                //NDBG_PRINT("%sTRY_WRITE%s: m_out_q->read_ptr(): %p m_out_q->read_avail(): %d\n",
                //                ANSI_COLOR_FG_GREEN, ANSI_COLOR_OFF,
                //                m_out_q->read_ptr(),
                //                m_out_q->read_avail());
                l_bytes_written = ncwrite(m_out_q->b_read_ptr(), m_out_q->b_read_avail());
                //NDBG_PRINT("%sTRY_WRITE%s: l_bytes_written: %d\n", ANSI_COLOR_FG_GREEN, ANSI_COLOR_OFF, l_bytes_written);
                if(l_bytes_written < 0)
                {
                        NDBG_PRINT("Error performing ncwrite: status: %d\n", l_bytes_written);
                        return NC_STATUS_ERROR;
                }
                if(m_write_cb &&
                  (l_bytes_written > 0))
                {
                        int32_t l_status = m_write_cb(m_data, m_out_q->b_read_ptr(), l_bytes_written);
                        if(l_status != STATUS_OK)
                        {
                                NDBG_PRINT("Error performing m_read_cb\n");
                                return NC_STATUS_ERROR;
                        }
                }
                // and not error?
                m_out_q->b_read_incr(l_bytes_written);
        } while(l_bytes_written > 0 && m_out_q->read_avail());
        return l_bytes_written;
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
bool nconn::can_reuse(void)
{
        //NDBG_PRINT("CONN ka num %ld / %ld \n", m_num_reqs, m_num_reqs_per_conn);
        if(((m_num_reqs_per_conn == -1) ||
            (m_num_reqs < m_num_reqs_per_conn)))
        {
                return true;
        }
        else
        {
                //NDBG_PRINT("CONN m_num_reqs: %ld m_num_reqs_per_conn: %ld \n",
                //                m_num_reqs,
                //                m_num_reqs_per_conn);
                return false;
        }
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
int32_t nconn::nc_set_listening(evr_loop *a_evr_loop, int32_t a_val)
{
        //NDBG_PRINT("%sRUN_STATE_MACHINE%s: SET_LISTENING[%d]\n", ANSI_COLOR_BG_RED, ANSI_COLOR_OFF, a_val);
        int32_t l_status;
        l_status = ncset_listening(a_evr_loop, a_val);
        if(l_status != NC_STATUS_OK)
        {
                return STATUS_ERROR;
        }

        m_nc_state = NC_STATE_LISTENING;
        return STATUS_OK;
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
int32_t nconn::nc_set_accepting(evr_loop *a_evr_loop, int a_fd)
{
        int32_t l_status;
        l_status = ncset_accepting(a_evr_loop, a_fd);
        if(l_status != NC_STATUS_OK)
        {
                return STATUS_ERROR;
        }

        m_nc_state = NC_STATE_ACCEPTING;
        return STATUS_OK;
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
int32_t nconn::nc_cleanup()
{
        //NDBG_PRINT("%s--CONN--%s last_state: %d\n", ANSI_COLOR_BG_RED, ANSI_COLOR_OFF, m_nc_state);
        int32_t l_status;
        l_status = nccleanup();
        m_nc_state = NC_STATE_FREE;
        m_num_reqs = 0;
        if(l_status != NC_STATUS_OK)
        {
                NDBG_PRINT("Error performing nccleanup.\n");
                return STATUS_ERROR;
        }

        // Set data to null
        m_data = NULL;
        m_in_q = NULL;
        m_out_q = NULL;

        return STATUS_OK;
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
nconn::nconn(void):
              m_scheme(SCHEME_NONE),
              m_host(),
              m_stat(),
              m_collect_stats_flag(false),
              m_data(NULL),
              m_connect_start_time_us(0),
              m_request_start_time_us(0),
              m_last_error(""),
              m_host_info(),
              m_num_reqs_per_conn(-1),
              m_num_reqs(0),
              m_connect_only(false),
              m_in_q(NULL),
              m_out_q(NULL),
              m_nc_state(NC_STATE_FREE),
              m_id(0),
              m_idx(0),
              m_read_cb(NULL),
              m_write_cb(NULL)
{
        // Set stats
        if(m_collect_stats_flag)
        {
                stat_init(m_stat);
        }
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
nconn::~nconn(void)
{
}

} // ns_hlx