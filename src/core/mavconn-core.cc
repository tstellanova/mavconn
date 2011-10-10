/*=====================================================================

MAVCONN Micro Air Vehicle Flying Robotics Toolkit
Please see our website at <http://MAVCONN.ethz.ch>

(c) 2009 MAVCONN PROJECT

This file is part of the MAVCONN project

    MAVCONN is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    MAVCONN is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with MAVCONN. If not, see <http://www.gnu.org/licenses/>.

======================================================================*/

/**
 * @file
 *   @brief LCM example
 *
 *   @author Lorenz Meier <mavteam@student.ethz.ch>
 *
 */

// BOOST includes

#include <cstdio>
#include <iostream>
#include <glib.h>
#include <stdio.h>
#include <glibtop.h>
#include <glibtop/cpu.h>
#include <glibtop/mem.h>
#include <glibtop/proclist.h>

// MAVLINK message format includes
#include "mavconn.h"
#include "core/MAVConnParamClient.h"

// Latency Benchmarking
#include <sys/time.h>
#include <time.h>

// Timer for benchmarking
struct timeval tv;

using std::string;
using namespace std;

#define COMM_READY_TIMEOUT 200
#define COMM_GCS_TIMEOUT 2000

enum COMM_STATE
{
	COMM_STATE_UNINIT=0,
	COMM_STATE_BOOT,
	COMM_STATE_READY,
	COMM_STATE_GCS_ESTABLISHED
};

enum COMPONENT_STATE
{
	CMP_STATE_UNINIT=0,
	CMP_STATE_OK
};

typedef struct
{
	uint8_t core;
	uint8_t comm;
	uint8_t comm_errors;
	uint8_t subsys_errors;
} system_state_t;

system_state_t static inline mk_system_state_t()
{
	system_state_t s;
	s.core = MAV_STATE_UNINIT;
	s.comm = COMM_STATE_UNINIT;
	s.comm_errors = 0;
	s.subsys_errors = 0;
	return s;
}


// Settings
int systemid = getSystemID();		///< The unique system id of this MAV, 0-255. Has to be consistent across the system
int compid = MAV_COMP_ID_SYSTEM_CONTROL;		///< The unique component id of this process.
int systemType = MAV_TYPE_QUADROTOR;		///< The system type
bool silent = false;				///< Wether console output should be enabled
bool verbose = false;				///< Enable verbose output
bool emitHeartbeat = false;			///< Generate a heartbeat with this process
bool emitLoad = false;				///< Emit CPU load as debug message 101
bool debug = false;					///< Enable debug functions and output
bool cpu_performance = false;		///< Set CPU to performance mode (needs root)

mavlink_heartbeat_t heartbeat;

uint64_t currTime;
uint64_t lastTime;

uint64_t lastGCSTime;

MAVConnParamClient* paramClient;

static void mavlink_handler(const lcm_recv_buf_t *rbuf, const char * channel,const mavconn_mavlink_msg_container_t* container, void * user)
{
	if (debug) printf("Received message on channel \"%s\":\n", channel);

	lcm_t* lcm = (lcm_t*)user;
	const mavlink_message_t* msg = getMAVLinkMsgPtr(container);

	// Handle param messages
	paramClient->handleMAVLinkPacket(msg);

	switch(msg->msgid)
	{
	case MAVLINK_MSG_ID_COMMAND_SHORT:
	{
		mavlink_command_short_t cmd;
		mavlink_msg_command_short_decode(msg, &cmd);
		if (cmd.target_system == getSystemID())
		{
			switch (cmd.command)
			{
			case MAV_CMD_PREFLIGHT_REBOOT_SHUTDOWN:
			{
				int ret = 0;
				if (cmd.param2 == 2)
				{
					if (verbose) printf("Shutdown received, shutting down system\n");
					ret = system ("halt");
					if (ret) if (verbose) std::cerr << "Shutdown failed." << std::endl;
				}
				else if (cmd.param2 == 1)
				{
					if (verbose) printf("Reboot received, rebooting system\n");
					ret = system ("reboot");
					if(ret) if (verbose) std::cerr << "Reboot failed." << std::endl;
				}

				if (cmd.confirmation)
				{
					mavlink_message_t response;
					mavlink_command_ack_t ack;
					ack.command = cmd.command;
					if (ret == 0)
					{
						ack.result = 0;
					}
					else
					{
						ack.result = 1;
					}
					mavlink_msg_command_ack_encode(getSystemID(), compid, &response, &ack);
					sendMAVLinkMessage(lcm, &response);
				}
			}
			break;
			}
		}
	}
	break;
	case MAVLINK_MSG_ID_COMMAND_LONG:
		{

		}
		break;
	case MAVLINK_MSG_ID_HEARTBEAT:
	{
		switch(mavlink_msg_heartbeat_get_type(msg))
		{
		case MAV_TYPE_GCS:
			gettimeofday(&tv, NULL);
			uint64_t currTime =  ((uint64_t)tv.tv_sec) * 1000000 + tv.tv_usec;
			// Groundstation present
			lastGCSTime = currTime;
			if (verbose) std::cout << "Heartbeat received from GCS/OCU " << msg->sysid;
			break;
		}
	}
	break;
	case MAVLINK_MSG_ID_PING:
	{
		mavlink_ping_t ping;
		mavlink_msg_ping_decode(msg, &ping);
		uint64_t r_timestamp = getSystemTimeUsecs();
		if (ping.target_system == 0 && ping.target_component == 0)
		{
			mavlink_message_t r_msg;
			mavlink_msg_ping_pack(systemid, compid, &r_msg, ping.seq, msg->sysid, msg->compid, r_timestamp);
			sendMAVLinkMessage(lcm, &r_msg);
		}
	}
	break;
	default:
		if (debug) printf("ERROR: could not decode message with ID: %d\n", msg->msgid);
		break;
	}
}

void* lcm_wait(void* lcm_ptr)
				{
	lcm_t* lcm = (lcm_t*) lcm_ptr;
	// Blocking wait for new data
	while (1)
	{
		lcm_handle (lcm);
	}
	return NULL;
				}

int main (int argc, char ** argv)
{
	// Handling Program options
	static GOptionEntry entries[] =
	{
			{ "sysid", 'a', 0, G_OPTION_ARG_INT, &systemid, "ID of this system", NULL },
			{ "compid", 'c', 0, G_OPTION_ARG_INT, &compid, "ID of this component", NULL },
			{ "heartbeat", NULL, 0, G_OPTION_ARG_NONE, &emitHeartbeat, "Emit Heartbeat", (emitHeartbeat) ? "on" : "off" },
			{ "cpu", NULL, 0, G_OPTION_ARG_NONE, &cpu_performance, "Set CPU to performance mode", NULL },
			{ "load", 'l', 0, G_OPTION_ARG_NONE, &emitLoad, "Emit CPU load as debug message 101", NULL },
			{ "silent", 's', 0, G_OPTION_ARG_NONE, &silent, "Be silent", NULL },
			{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Be verbose", NULL },
			{ "debug", 'd', 0, G_OPTION_ARG_NONE, &debug, "Debug mode, changes behaviour", NULL },
			{ NULL }
	};

	GError *error = NULL;
	GOptionContext *context;

	context = g_option_context_new ("- translate between LCM broadcast bus and ground control link");
	g_option_context_add_main_entries (context, entries, NULL);
	//g_option_context_add_group (context, NULL);
	if (!g_option_context_parse (context, &argc, &argv, &error))
	{
		g_print ("Option parsing failed: %s\n", error->message);
		exit (1);
	}

	lcm_t * lcm;

	lcm = lcm_create ("udpm://");
	if (!lcm)
		return 1;

	// Initialize parameter client before subscribing (and receiving) MAVLINK messages
//	paramClient = new PxParamClient(systemid, compid, lcm, "px_system_control.cfg", verbose);
//	paramClient->setParamValue("SYS_ID", systemid);
//	paramClient->readParamsFromFile("px_system_control.cfg");

	mavconn_mavlink_msg_container_t_subscription_t * commSub =
			mavconn_mavlink_msg_container_t_subscribe (lcm, MAVLINK_MAIN, &mavlink_handler, lcm);

	// Thread
	GThread* lcm_thread;
	GError* err;

	if( !g_thread_supported() )
	{
		g_thread_init(NULL);
		// Only initialize g thread if not already done
	}

	if( (lcm_thread = g_thread_create((GThreadFunc)lcm_wait, (void *)lcm, TRUE, &err)) == NULL)
	{
		printf("Thread create failed: %s!!\n", err->message );
		g_error_free ( err ) ;
	}

	// Initialize system information library
	glibtop_init();

	glibtop_cpu cpu;
	glibtop_mem memory;
	glibtop_proclist proclist;

	glibtop_get_cpu (&cpu);
	uint64_t old_total = cpu.total;
	uint64_t old_idle = cpu.idle;

	uint8_t baseMode;
	uint8_t customMode;
	uint8_t systemStatus;

	printf("\nPX SYSTEM CONTROL STARTED ON MAV %d (COMPONENT ID:%d) - RUNNING..\n\n", systemid, compid);

	while (1)
	{
		gettimeofday(&tv, NULL);
		uint64_t currTime =  ((uint64_t)tv.tv_sec) * 1000000 + tv.tv_usec;

		if (currTime - lastTime > 1000000)
		{

			if (cpu_performance)
			{
				//set cpu to always full power
				system("echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
				system("echo performance > /sys/devices/system/cpu/cpu1/cpufreq/scaling_governor");
			}

			// SEND OUT TIME MESSAGE
			// send message as close to time aquisition as possible
			mavlink_message_t msg;

			// Send heartbeat if enabled
			if (emitHeartbeat)
			{
				mavlink_msg_system_time_pack(systemid, compid, &msg, currTime, 0);
				sendMAVLinkMessage(lcm, &msg);

				lastTime = currTime;
				if (verbose) std::cout << "Emitting heartbeat" << std::endl;

				// SEND HEARTBEAT

				// Pack message and get size of encoded byte string
				mavlink_msg_heartbeat_pack(systemid, compid, &msg, systemType, MAV_AUTOPILOT_PIXHAWK, baseMode, customMode, systemStatus);
				sendMAVLinkMessage(lcm, &msg);
			}

			if (emitLoad)
			{
				// GET SYSTEM INFORMATION
				glibtop_get_cpu (&cpu);
				glibtop_get_mem(&memory);
				float load = ((float)(cpu.total-old_total)-(float)(cpu.idle-old_idle)) / (float)(cpu.total-old_total);
//				mavlink_msg_debug_pack(systemid, compid, &msg, 101, load*100.f);
//				sendMAVLinkMessage(lcm, &msg);
				// FIXME V10
				old_total = cpu.total;
				old_idle = cpu.idle;

				if (verbose)
				{
					printf("CPU TYPE INFORMATIONS \n\n"
							"Cpu Total : %ld \n"
							"Cpu User : %ld \n"
							"Cpu Nice : %ld \n"
							"Cpu Sys : %ld \n"
							"Cpu Idle : %ld \n"
							"Cpu Frequences : %ld \n",
							(unsigned long)cpu.total,
							(unsigned long)cpu.user,
							(unsigned long)cpu.nice,
							(unsigned long)cpu.sys,
							(unsigned long)cpu.idle,
							(unsigned long)cpu.frequency);

					printf("\nLOAD: %f %%\n\n", load*100.0f);

					printf("\nMEMORY USING\n\n"
							"Memory Total : %ld MB\n"
							"Memory Used : %ld MB\n"
							"Memory Free : %ld MB\n"
							"Memory Shared: %ld MB\n"
							"Memory Buffered : %ld MB\n"
							"Memory Cached : %ld MB\n"
							"Memory user : %ld MB\n"
							"Memory Locked : %ld MB\n",
							(unsigned long)memory.total/(1024*1024),
							(unsigned long)memory.used/(1024*1024),
							(unsigned long)memory.free/(1024*1024),
							(unsigned long)memory.shared/(1024*1024),
							(unsigned long)memory.buffer/(1024*1024),
							(unsigned long)memory.cached/(1024*1024),
							(unsigned long)memory.user/(1024*1024),
							(unsigned long)memory.locked/(1024*1024));

					int which = 0, arg = 0;
					glibtop_get_proclist(&proclist,which,arg);
					printf("%ld\n%ld\n%ld\n",
							(unsigned long)proclist.number,
							(unsigned long)proclist.total,
							(unsigned long)proclist.size);
				}
			}
		}

		//sleep as long as possible, if getting near to 1 second sleep only short
		gettimeofday(&tv, NULL);
		uint64_t currTime2 =  ((uint64_t)tv.tv_sec) * 1000000 + tv.tv_usec;
		int64_t sleeptime = 900000 - (currTime2-currTime);
		if (sleeptime > 0)
			usleep(sleeptime);
		else
			usleep(10000);
	}

	mavconn_mavlink_msg_container_t_unsubscribe (lcm, commSub);
	lcm_destroy (lcm);

	return 0;
}
