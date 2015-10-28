/**
  @file wl1251-cal.c

  Copyright (C) 2012 Jonathan Wilson

  @author Jonathan wilson <jfwfreo@tpgi.com.au>

  This prorgam is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License version 2.1 as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this library; if not, write to the Free Software Foundation,
  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/
#define _BSD_SOURCE
#include <net/if.h>
#include <net/if_arp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <cal.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
int wl1251_set_mac_address(char *iface,unsigned char *address)
{
	struct ifreq ifr;
	int skfd;
	snprintf(ifr.ifr_name, IFNAMSIZ, "%s", iface);
	ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
	ifr.ifr_hwaddr.sa_data[0] = address[5];
	ifr.ifr_hwaddr.sa_data[1] = address[4];
	ifr.ifr_hwaddr.sa_data[2] = address[3];
	ifr.ifr_hwaddr.sa_data[3] = address[2];
	ifr.ifr_hwaddr.sa_data[4] = address[1];
	ifr.ifr_hwaddr.sa_data[5] = address[0];
	ifr.ifr_hwaddr.sa_data[6] = 0;
	skfd = socket(PF_INET,SOCK_STREAM,0);
	if (skfd == -1)
	{
		return 0;
	}
	if (ioctl(skfd,SIOCSIFHWADDR, &ifr) < 0)
	{
		perror("ioctl");
		return 0;
	}
	close(skfd);
	return 1;
}
static int error_handler(struct sockaddr_nl *nla,struct nlmsgerr *err, void *arg)
{
	int *ret = arg;
	*ret = err->error;
	return NL_STOP;
}

static int ack_handler(struct nl_msg *msg, void *arg)
{
	int *ret = arg;
	*ret = 0;
	return NL_STOP;
}

static int finish_handler(struct nl_msg *msg, void *arg)
{
	int *ret = arg;
	arg = 0;
	return NL_SKIP;
}

int wl1251_push_nvs(char *iface,unsigned char *nvs, int nvs_size)
{
	struct nl_handle *nlh = nl_handle_alloc();
	if (!nlh)
	{
		return 0;
	}
	if (genl_connect(nlh))
	{
		nl_handle_destroy(nlh);
		return 0;
	}
	int id = genl_ctrl_resolve(nlh,"wl1251");
	if (id < 0)
	{
		nl_handle_destroy(nlh);
		return 0;
	}
	struct nl_msg *msg = nlmsg_alloc();
	if (!msg)
	{
		return 0;
	}
	if (!genlmsg_put(msg,getpid(),0,id,0,0,5,1))
	{
		nlmsg_free(msg);
		nl_handle_destroy(nlh);
		return 0;
	}
	if (nla_put(msg,1,strlen(iface)+1,iface) < 0)
	{
		nlmsg_free(msg);
		nl_handle_destroy(nlh);
		return 0;
	}
	if (nla_put(msg,0xA,nvs_size,nvs) < 0)
	{
		nlmsg_free(msg);
		nl_handle_destroy(nlh);
		return 0;
	}
	if (nla_put(msg,0xB,4,&nvs_size) < 0)
	{
		nlmsg_free(msg);
		nl_handle_destroy(nlh);
		return 0;
	}
	struct nl_cb *cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!cb)
	{
		nlmsg_free(msg);
		nl_handle_destroy(nlh);
		return 0;
	}
	int complete = nl_send_auto_complete(nlh,msg);
	if (complete < 0)
	{
		nl_cb_put(cb);
		nlmsg_free(msg);
		nl_handle_destroy(nlh);
		return 0;
	}
	int ret = 1;
	nl_cb_err(cb,NL_CB_CUSTOM,error_handler,&ret);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &ret);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &ret);
	while (ret > 0)
	{
		nl_recvmsgs(nlh,cb);
	}
	nl_cb_put(cb);
	nlmsg_free(msg);
	nl_handle_destroy(nlh);
	return 1;
}

int wl1251_set_regdomain(char *countrycode)
{
	struct nl_handle *nlh = nl_handle_alloc();
	if (!nlh)
	{
		return 0;
	}
	if (genl_connect(nlh))
	{
		nl_handle_destroy(nlh);
		return 0;
	}
	int id = genl_ctrl_resolve(nlh,"nl80211");
	if (id < 0)
	{
		nl_handle_destroy(nlh);
		return 0;
	}
	struct nl_msg *msg = nlmsg_alloc();
	if (!msg)
	{
		return 0;
	}
	if (!genlmsg_put(msg,getpid(),0,id,0,0,0x1B,0))
	{
		nlmsg_free(msg);
		nl_handle_destroy(nlh);
		return 0;
	}
	if (nla_put(msg,0x21,3,countrycode) < 0)
	{
		nlmsg_free(msg);
		nl_handle_destroy(nlh);
		return 0;
	}
	struct nl_cb *cb = nl_cb_alloc(NL_CB_CUSTOM);
	if (!cb)
	{
		nlmsg_free(msg);
		nl_handle_destroy(nlh);
		return 0;
	}
	int complete = nl_send_auto_complete(nlh,msg);
	if (complete < 0)
	{
		nl_cb_put(cb);
		nlmsg_free(msg);
		nl_handle_destroy(nlh);
		return 0;
	}
	int ret = 1;
	nl_cb_err(cb,NL_CB_CUSTOM,error_handler,&ret);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &ret);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &ret);
	while (ret > 0)
	{
		nl_recvmsgs(nlh,cb);
	}
	nl_cb_put(cb);
	nlmsg_free(msg);
	nl_handle_destroy(nlh);
	return 1;
}

int main()
{
	struct cal *c;
	unsigned char *data;
	unsigned long size;
        cal_init(&c);
	cal_read_block(c,"cert-npc",(void **)&data,&size,0);
	cal_finish(c);
	data += 0x94;
        int count = data[0] | data[1] << 8 | data[2] << 16 | data[3] << 24;
	data += 4;
        for (int i = 0;i < count;i++)
	{
		if (!strcmp(data,"WLAN_ID"))
		{
			data += 8;
			if (!wl1251_set_mac_address("wlan0",data))
			{
				return 1;
			}
			data += 32;
		}
		else
		{
			data += 40;
		}
	}
        cal_init(&c);
	cal_read_block(c,"cert-ccc",(void **)&data,&size,0);
	cal_finish(c);
	data += 368;
        int count2 = data[0] | data[1] << 8 | data[2] << 16 | data[3] << 24;
	data += 4;
	int fcc = 0;
	for (int i = 0; i < (count2 / 4);i++)
	{
		short s = data[0] | data[1] << 8;
		data += 2;
		if (s == 0)
		{
			s = data[0] | data[1] << 8;
			if (s == 2)
			{
				fcc = 1;
			}
		}
		data += 2;
	}
	char *cc = "EU";
	if (fcc)
	{
		cc = "US";
	}
	wl1251_set_regdomain(cc);
	printf("country is %s\n",cc);
        cal_init(&c);

	cal_read_block(c,"wlan-tx-cost3_0",(void **)&data,&size,0);
	cal_finish(c);
	if (fcc)
	{
		data[377] = 2;
		data[337] = 2;
		data[380] = 9;
		data[340] = 9;
	}
	wl1251_push_nvs("wlan0",data + 4,752);
	return 0;
}
