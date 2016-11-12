/**
  @file wl1251-cal.c

  Copyright (C) 2012 Jonathan Wilson <jfwfreo@tpgi.com.au>
  Copyright (C) 2016 Pali Rohár <pali.rohar@gmail.com>

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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <net/if.h>
#include <net/if_arp.h>

#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

#include <linux/nl80211.h>

#include <dbus/dbus.h>

#include <cal.h>

#define WL1251_NL_NAME "wl1251"
#define WL1251_NL_VERSION 1

enum wl1251_nl_commands {
	WL1251_NL_CMD_UNSPEC = 0,
	WL1251_NL_CMD_TEST,
	WL1251_NL_CMD_INTERROGATE,
	WL1251_NL_CMD_CONFIGURE,
	WL1251_NL_CMD_PHY_REG_READ,
	WL1251_NL_CMD_NVS_PUSH,
	WL1251_NL_CMD_REG_WRITE,
	WL1251_NL_CMD_REG_READ,
	WL1251_NL_CMD_SET_PLT_MODE,
};

enum wl1251_nl_attrs {
	WL1251_NL_ATTR_UNSPEC = 0,
	WL1251_NL_ATTR_IFNAME,
	WL1251_NL_ATTR_CMD_TEST_PARAM,
	WL1251_NL_ATTR_CMD_TEST_ANSWER,
	WL1251_NL_ATTR_CMD_IE,
	WL1251_NL_ATTR_CMD_IE_LEN,
	WL1251_NL_ATTR_CMD_IE_BUFFER,
	WL1251_NL_ATTR_CMD_IE_ANSWER,
	WL1251_NL_ATTR_REG_ADDR,
	WL1251_NL_ATTR_REG_VAL,
	WL1251_NL_ATTR_NVS_BUFFER,
	WL1251_NL_ATTR_NVS_LEN,
	WL1251_NL_ATTR_PLT_MODE,
};

struct code_domain {
	int country_code;
	char regdomain[3];
};

static int wl1251_set_mac_address(char *iface, unsigned char *address)
{
	struct ifreq ifr;
	int skfd;
	int len;

	len = strlen(iface);
	if (len >= IFNAMSIZ) {
		fprintf(stderr, "wl1251-cal: bad interface name %s\n", iface);
		return -1;
	}

	memcpy(ifr.ifr_name, iface, len+1);
	ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
	ifr.ifr_hwaddr.sa_data[0] = address[5];
	ifr.ifr_hwaddr.sa_data[1] = address[4];
	ifr.ifr_hwaddr.sa_data[2] = address[3];
	ifr.ifr_hwaddr.sa_data[3] = address[2];
	ifr.ifr_hwaddr.sa_data[4] = address[1];
	ifr.ifr_hwaddr.sa_data[5] = address[0];
	ifr.ifr_hwaddr.sa_data[6] = 0;

	skfd = socket(PF_INET, SOCK_STREAM, 0);
	if (skfd < 0) {
		perror("wl1251-cal: could not open socket for ioctl");
		return -1;
	}

	if (ioctl(skfd, SIOCSIFHWADDR, &ifr) < 0) {
		perror("wl1251-cal: ioctl SIOCSIFHWADDR failed");
		close(skfd);
		return -1;
	}

	close(skfd);
	return 0;
}

static int wl1251_nl_push_regdomain(struct nl_handle *nlh, char *regdomain)
{
	struct nl_msg *msg = NULL;
	int family = -1;
	int ret = -1;

	family = genl_ctrl_resolve(nlh, "nl80211");
	if (family < 0) {
		fprintf(stderr, "wl1251-cal: didn't find nl80211 netlink control\n");
		goto out;
	}

	printf("wl1251-cal: nl80211 netlink family id %d\n", family);

	msg = nlmsg_alloc();
	if (!msg) {
		nl_perror("wl1251-cal: failed to alloc netlink message NL80211_CMD_REQ_SET_REG");
		goto out;
	}

	if (!genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, 0, NL80211_CMD_REQ_SET_REG, 0)) {
		nl_perror("wl1251-cal: failed to gen netlink message NL80211_CMD_REQ_SET_REG");
		goto out;
	}

	if (nla_put(msg, NL80211_ATTR_REG_ALPHA2, 2, regdomain) < 0) {
		nl_perror("wl1251-cal: failed to put netlink message NL80211_CMD_REQ_SET_REG");
		goto out;
	}

	if (nl_send_auto_complete(nlh, msg) < 0) {
		nl_perror("wl1251-cal: failed to send netlink message NL80211_CMD_REQ_SET_REG");
		goto out;
	}

	ret = 0;

out:
	nlmsg_free(msg);
	return ret;
}

static int wl1251_nl_push_nvs(struct nl_handle *nlh, char *iface, unsigned char *nvs, uint32_t nvs_size)
{
	struct nl_msg *msg = NULL;
	int family = -1;
	int ret = -1;

	family = genl_ctrl_resolve(nlh, WL1251_NL_NAME);
	if (family < 0) {
		fprintf(stderr, "wl1251-cal: didn't find wl1251 netlink control\n");
		goto out;
	}

	printf("wl1251-cal: " WL1251_NL_NAME " netlink family id is %d\n", family);

	msg = nlmsg_alloc();
	if (!msg) {
		nl_perror("wl1251-cal: failed to alloc netlink message WL1251_NL_CMD_NVS_PUSH");
		goto out;
	}

	if (!genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, 0, WL1251_NL_CMD_NVS_PUSH, WL1251_NL_VERSION)) {
		nl_perror("wl1251-cal: failed to gen netlink message WL1251_NL_CMD_NVS_PUSH");
		goto out;
	}

	if (nla_put(msg, WL1251_NL_ATTR_IFNAME, strlen(iface)+1, iface) < 0) {
		nl_perror("wl1251-cal: failed to put netlink message WL1251_NL_CMD_NVS_PUSH");
		goto out;
	}

	if (nla_put(msg, WL1251_NL_ATTR_NVS_BUFFER, nvs_size, nvs) < 0) {
		nl_perror("wl1251-cal: failed to put netlink message WL1251_NL_CMD_NVS_PUSH");
		goto out;
	}

	if (nla_put(msg, WL1251_NL_ATTR_NVS_LEN, 4, &nvs_size) < 0) {
		nl_perror("wl1251-cal: failed to put netlink message WL1251_NL_CMD_NVS_PUSH");
		goto out;
	}

	if (nl_send_auto_complete(nlh, msg) < 0) {
		nl_perror("wl1251-cal: failed to send netlink message WL1251_NL_CMD_NVS_PUSH");
		goto out;
	}

	ret = 0;

out:
	nlmsg_free(msg);
	return ret;
}

static struct nl_handle *wl1251_nl_connect(void)
{
	struct nl_handle *nlh;

	nlh = nl_handle_alloc();
	if (!nlh) {
		nl_perror("wl1251-cal: failed to alloc netlink");
		return NULL;
	}

	if (genl_connect(nlh)) {
		nl_perror("wl1251-cal: failed to connect netlink");
		nl_handle_destroy(nlh);
		return NULL;
	}

	return nlh;
}

static void wl1251_nl_destroy(struct nl_handle *nlh)
{
	nl_handle_destroy(nlh);
}

static int error_handler(struct sockaddr_nl *nla, struct nlmsgerr *err, void *arg)
{
	printf("wl1251-cal: error_handler()\n");
	*((int *)arg) = err->error;
	return NL_STOP;
}

static int ack_handler(struct nl_msg *msg, void *arg)
{
	printf("wl1251-cal: ack_handler()\n");
	*((int *)arg) = 0;
	return NL_STOP;
}

static int finish_handler(struct nl_msg *msg, void *arg)
{
	printf("wl1251-cal: finish_handler()\n");
	*((int *)arg) = 0;
	return NL_SKIP;
}

static int wl1251_nl_receive(struct nl_handle *nlh)
{
	struct nl_cb *cb;
	int ret;

	cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!cb) {
		nl_perror("wl1251-cal: nl_cb_alloc failed");
		return -1;
	}

	nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &ret);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &ret);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &ret);

	ret = 1;
	while (ret > 0) {
		if (nl_recvmsgs(nlh, cb) < 0)
			nl_perror("wl1251-cal: nl_recvmsgs failed");
	}

	nl_cb_put(cb);
	return ret;
}

static void wl1251_cal_read_address(struct cal *c, unsigned char *address)
{
	void *npc_ptr = NULL;
	unsigned long npc_len;
	unsigned char *npc;
	int npc_count;
	int have_address;
	int i;

	if (cal_read_block(c, "cert-npc", &npc_ptr, &npc_len, 0) < 0)
		npc_len = 0;

	have_address = 0;

	if (npc_len) {
		npc = npc_ptr + 0x94;
		npc_count = npc[0] | npc[1] << 8 | npc[2] << 16 | npc[3] << 24;
		npc += 4;
		for (i = 0; i < npc_count; i++) {
			if (memcmp(npc, "WLAN_ID", 8) == 0) {
				memcpy(address, npc+8, 6);
				printf("wl1251-cal: found MAC address %02x:%02x:%02x:%02x:%02x:%02x\n",
					address[5], address[4], address[3], address[2], address[1], address[0]);
				have_address = 1;
				break;
			}
			npc += 40;
		}
	}

	if (!have_address) {
		memset(address, 0, 6);
		fprintf(stderr, "wl1251-cal: couldn't read WLAN mac address from CAL\n");
	}

	free(npc_ptr);
}

static void wl1251_cal_read_fcc(struct cal *c, int *fcc)
{
	void *ccc_ptr = NULL;
	unsigned long ccc_len;
	unsigned char *ccc;
	int ccc_count;
	int i;

	if (cal_read_block(c, "cert-ccc", &ccc_ptr, &ccc_len, 0) < 0)
		ccc_len = 0;

	*fcc = 0;
	if (ccc_len) {
		ccc = ccc_ptr + 368;
		ccc_count = ccc[0] | ccc[1] << 8 | ccc[2] << 16 | ccc[3] << 24;
		ccc += 4;
		for (i = 0; i < (ccc_count / 4); i++) {
			if (ccc[0] == 0 && ccc[1] == 0 && ccc[2] == 2 && ccc[3] == 0)
				*fcc = 1;
			ccc += 4;
		}
	} else {
		fprintf(stderr, "wl1251-cal: couldn't read fcc from CAL\n");
	}

	free(ccc_ptr);
}

static void wl1251_cal_read_nvs(struct cal *c, unsigned char **nvs, unsigned long *nvs_len)
{
	void *nvs_ptr;

	if (cal_read_block(c, "wlan-tx-cost3_0", &nvs_ptr, nvs_len, 0) < 0)
		*nvs_len = 0;

	if (*nvs_len) {
		printf("wl1251-cal: Got CAL NVS\n");
		*nvs = nvs_ptr;
	} else {
		fprintf(stderr, "wl1251-cal: Couldnt get a CAL NVS, using default one\n");
		*nvs = NULL;
	}
}

static void wl1251_cal_read(unsigned char *address, int *fcc, unsigned char **nvs, unsigned long *nvs_len)
{
	struct cal *c;

	if (cal_init(&c) < 0) {
		fprintf(stderr, "wl1251-cal: cal_init failed\n");
		c = NULL;
	}

	wl1251_cal_read_address(c, address);
	wl1251_cal_read_fcc(c, fcc);

	if (nvs)
		wl1251_cal_read_nvs(c, nvs, nvs_len);

	if (c)
		cal_finish(c);
}

static void wl1251_vfs_read_nvs(unsigned char **nvs, unsigned long *nvs_len)
{
	int fd;
	int errno_old;
	off_t size;

	fd = open("/lib/firmware/ti-connectivity/wl1251-nvs.bin", O_RDONLY);
	if (fd < 0) {
		errno_old = errno;
		fd = open("/lib/firmware/wl1251-nvs.bin", O_RDONLY);
		if (fd < 0) {
			perror("wl1251-cal: Cannot open NVS file /lib/firmware/wl1251-nvs.bin");
			errno = errno_old;
			perror("wl1251-cal: Cannot open NVS file /lib/firmware/ti-connectivity/wl1251-nvs.bin");
			return;
		}
	}

	size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	if (size == 0 || size == (off_t)-1) {
		perror("wl1251-cal: Cannot determinate size of NVS file wl1251-nvs.bin");
		close(fd);
		*nvs = NULL;
		*nvs_len = 0;
		return;
	}

	*nvs = malloc(size+4);
	*nvs_len = size+4;
	if (!*nvs) {
		perror("wl1251-cal: malloc failed");
		close(fd);
		*nvs_len = 0;
		return;
	}

	if (read(fd, *nvs+4, size) != size) {
		perror("wl1251-cal: Cannot read NVS file wl1251-nvs.bin");
		close(fd);
		free(*nvs);
		*nvs = NULL;
		*nvs_len = 0;
		return;
	}

	printf("wl1251-cal: Got NVS from firmware directory\n");
	close(fd);
}

static int wl1251_vfs_read_regdomain(char *regdomain)
{
	FILE * stream;
	char buf[4];
	size_t len;
	int ret;

	stream = popen(". /etc/default/crda; echo $REGDOMAIN", "r");
	if (!stream)
		return 1;

	len = 0;

	if (fgets(buf, sizeof(buf), stream)) {
		len = strlen(buf);
		if (len > 0 && buf[len-1] == '\n')
			buf[--len] = 0;
	}

	ret = pclose(stream);
	if (ret != 0)
		return -1;

	if (len == 0) {
		fprintf(stderr, "wl1251-cal: REGDOMAIN in /etc/default/crda is not specified\n");
		return -1;
	} else if (len != 2) {
		fprintf(stderr, "wl1251-cal: REGDOMAIN in /etc/default/crda is incorrect\n");
		return -1;
	}

	memcpy(regdomain, buf, 3);
	return 0;
}

static int wl1251_csd_read_contry_code(DBusConnection *connection)
{
	DBusError error;
	DBusMessage *message;
	DBusMessage *reply;
	char status;
	dbus_uint16_t lac, network_type, supported_services;
	dbus_uint32_t cell_id, operator_code, country_code;
	dbus_int32_t error_value;

	message = dbus_message_new_method_call("com.nokia.phone.net", "/com/nokia/phone/net", "Phone.Net", "get_registration_status");
	if (!message) {
		fprintf(stderr, "wl1251-cal: Failed to ask registration status: %s\n", strerror(ENOMEM));
		return 0;
	}

	dbus_error_init(&error);
	reply = dbus_connection_send_with_reply_and_block(connection, message, 2000, &error);
	dbus_message_unref(message);
	if (dbus_error_is_set(&error)) {
		fprintf(stderr, "wl1251-cal: Failed to ask registration status: %s\n", error.message);
		dbus_error_free(&error);
		return 0;
	} else if (!reply) {
		fprintf(stderr, "wl1251-cal: Failed to ask registration status\n");
		return 0;
	}

	dbus_error_init(&error);
	if (!dbus_message_get_args(reply, &error,
					DBUS_TYPE_BYTE, &status,
					DBUS_TYPE_UINT16, &lac,
					DBUS_TYPE_UINT32, &cell_id,
					DBUS_TYPE_UINT32, &operator_code,
					DBUS_TYPE_UINT32, &country_code,
					DBUS_TYPE_BYTE, &network_type,
					DBUS_TYPE_BYTE, &supported_services,
					DBUS_TYPE_INT32, &error_value,
					DBUS_TYPE_INVALID)) {
		fprintf(stderr, "wl1251-cal: Could not get args from reply, '%s'\n", error.message);
		dbus_error_free(&error);
		dbus_message_unref(reply);
		return 0;
	}

	dbus_message_unref(reply);
	printf("wl1251-cal: Country code: %u\n", (unsigned int)country_code);
	return country_code;
}

static int wl1251_ofono_read_country_code(DBusConnection *connection)
{
	/* TODO: see https://git.kernel.org/cgit/network/ofono/ofono.git/tree/test/get-serving-cell-info */
	return 0;
}

/*
 Generate:
 $ sed 's/ \t/|/' /usr/share/operator-wizard/mcc_mapping | sort -t'|' -k2 > mcc_mapping
 $ sort -t'|' -k4 /usr/share/clock/wdb > wdb
 $ join -1 2 -2 4 -t'|' -o1.1,2.3 mcc_mapping wdb | sort -u | sed 's/^/{ /;s/|/, "/;s/$/" },/'
*/
static const struct code_domain codes[] = {
	{ 202, "GR" }, { 204, "NL" }, { 206, "BE" }, { 208, "FR" }, { 212, "MC" }, { 213, "AD" },
	{ 214, "ES" }, { 216, "HU" }, { 218, "BA" }, { 219, "HR" }, { 220, "RS" }, { 222, "IT" },
	{ 225, "VA" }, { 226, "RO" }, { 228, "CH" }, { 230, "CZ" }, { 231, "SK" }, { 232, "AT" },
	{ 234, "GB" }, { 235, "GB" }, { 238, "DK" }, { 240, "SE" }, { 242, "NO" }, { 244, "FI" },
	{ 246, "LT" }, { 247, "LV" }, { 248, "EE" }, { 250, "RU" }, { 255, "UA" }, { 257, "BY" },
	{ 259, "MD" }, { 260, "PL" }, { 262, "DE" }, { 266, "GI" }, { 268, "PT" }, { 270, "LU" },
	{ 272, "IE" }, { 274, "IS" }, { 276, "AL" }, { 278, "MT" }, { 280, "CY" }, { 282, "GE" },
	{ 283, "AM" }, { 284, "BG" }, { 286, "TR" }, { 288, "FO" }, { 290, "GL" }, { 292, "SM" },
	{ 293, "SI" }, { 294, "MK" }, { 295, "LI" }, { 297, "ME" }, { 302, "CA" }, { 308, "FR" },
	{ 310, "US" }, { 311, "US" }, { 312, "US" }, { 313, "US" }, { 314, "US" }, { 315, "US" },
	{ 316, "US" }, { 330, "PR" }, { 332, "US" }, { 334, "MX" }, { 338, "JM" }, { 340, "FR" },
	{ 342, "BB" }, { 344, "AG" }, { 346, "KY" }, { 348, "VG" }, { 350, "BM" }, { 352, "GD" },
	{ 354, "MS" }, { 356, "KN" }, { 358, "LC" }, { 360, "VC" }, { 362, "NL" }, { 363, "AW" },
	{ 364, "BS" }, { 365, "AI" }, { 366, "DM" }, { 368, "CU" }, { 370, "DO" }, { 372, "HT" },
	{ 374, "TT" }, { 376, "TC" }, { 400, "AZ" }, { 401, "KZ" }, { 402, "BT" }, { 404, "IN" },
	{ 405, "IN" }, { 410, "PK" }, { 412, "AF" }, { 413, "LK" }, { 414, "MM" }, { 415, "LB" },
	{ 416, "JO" }, { 417, "SY" }, { 418, "IQ" }, { 419, "KW" }, { 420, "SA" }, { 421, "YE" },
	{ 422, "OM" }, { 424, "AE" }, { 425, "IL" }, { 426, "BH" }, { 427, "QA" }, { 428, "MN" },
	{ 429, "NP" }, { 430, "AE" }, { 431, "AE" }, { 432, "IR" }, { 434, "UZ" }, { 436, "TJ" },
	{ 437, "KG" }, { 438, "TM" }, { 440, "JP" }, { 441, "JP" }, { 450, "KR" }, { 452, "VN" },
	{ 455, "MO" }, { 456, "KH" }, { 457, "LA" }, { 460, "CN" }, { 460, "HK" }, { 466, "TW" },
	{ 467, "KP" }, { 470, "BD" }, { 472, "MV" }, { 502, "MY" }, { 505, "AU" }, { 510, "ID" },
	{ 514, "TL" }, { 515, "PH" }, { 520, "TH" }, { 525, "SG" }, { 528, "BN" }, { 530, "NZ" },
	{ 535, "GU" }, { 536, "NR" }, { 537, "PG" }, { 539, "TO" }, { 540, "SB" }, { 541, "VU" },
	{ 542, "FJ" }, { 543, "WF" }, { 544, "WS" }, { 545, "KI" }, { 546, "FR" }, { 547, "PF" },
	{ 548, "CK" }, { 549, "WS" }, { 550, "FM" }, { 550, "MP" }, { 551, "MH" }, { 552, "PW" },
	{ 602, "EG" }, { 603, "DZ" }, { 604, "MA" }, { 605, "TN" }, { 606, "LY" }, { 607, "GM" },
	{ 608, "SN" }, { 609, "MR" }, { 610, "ML" }, { 611, "GN" }, { 612, "CI" }, { 613, "BF" },
	{ 614, "NE" }, { 615, "TG" }, { 616, "BJ" }, { 617, "MU" }, { 618, "LR" }, { 619, "SL" },
	{ 620, "GH" }, { 621, "NG" }, { 622, "TD" }, { 623, "CF" }, { 624, "CM" }, { 625, "CV" },
	{ 626, "ST" }, { 627, "GQ" }, { 628, "GA" }, { 629, "CG" }, { 630, "CD" }, { 631, "AO" },
	{ 632, "GW" }, { 633, "SC" }, { 634, "SD" }, { 635, "RW" }, { 636, "ET" }, { 637, "SO" },
	{ 638, "DJ" }, { 639, "KE" }, { 640, "TZ" }, { 641, "UG" }, { 642, "BI" }, { 643, "MZ" },
	{ 645, "ZM" }, { 646, "MG" }, { 647, "FR" }, { 648, "ZW" }, { 649, "NA" }, { 650, "MW" },
	{ 651, "LS" }, { 652, "BW" }, { 653, "SZ" }, { 654, "KM" }, { 655, "ZA" }, { 657, "ER" },
	{ 702, "BZ" }, { 704, "GT" }, { 706, "SV" }, { 708, "HN" }, { 710, "NI" }, { 712, "CR" },
	{ 714, "PA" }, { 716, "PE" }, { 722, "AR" }, { 724, "BR" }, { 730, "CL" }, { 732, "CO" },
	{ 734, "VE" }, { 736, "BO" }, { 738, "GY" }, { 740, "EC" }, { 742, "FR" }, { 744, "PY" },
	{ 746, "SR" }, { 748, "UY" }
};

static const int fcc_codes[] = { 302, 310, 311, 316, 312, 313, 314, 315, 332, 466, 724, 722, 334, 732 };

static void wl1251_country_code_to_regdomain(int country_code, int fcc, char *regdomain)
{
	unsigned int i;

	if (country_code == 0 && fcc) {
		printf("wl1251-cal: FCC country\n");
		memcpy(regdomain, "US", 3);
		return;
	}

	for (i = 0; i < sizeof(fcc_codes)/sizeof(fcc_codes[0]); ++i) {
		if (fcc_codes[i] == country_code) {
			printf("wl1251-cal: FCC country\n");
			memcpy(regdomain, "US", 3);
			return;
		}
	}

	for (i = 0; i < sizeof(codes)/sizeof(codes[0]); ++i) {
		if (codes[i].country_code == country_code) {
			printf("wl1251-cal: Regulatory domain: %s\n", codes[i].regdomain);
			memcpy(regdomain, codes[i].regdomain, 3);
			return;
		}
	}

	printf("wl1251-cal: Fallback regulatory domain: EU\n");
	memcpy(regdomain, "EU", 3);
}

static unsigned char default_nvs[] = {
	0x00, 0x00, 0x00, 0x00, 0x02, 0x11, 0x56, 0x06, 0x1c, 0x06, 0x01, 0x16, 0x60, 0x03,
	0x07, 0x01, 0x09, 0x56, 0x12, 0x00, 0x00, 0x00, 0x01, 0x0d, 0x56, 0x40, 0x00, 0x00,
	0x00, 0x02, 0x6d, 0x54, 0x09, 0x03, 0x07, 0x20, 0x00, 0x00, 0x00, 0x00, 0x01, 0x15,
	0x58, 0xa4, 0x00, 0x00, 0x00, 0x01, 0x31, 0x56, 0x02, 0x02, 0x00, 0x00, 0x01, 0x35,
	0x56, 0x04, 0xd1, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
	0x62, 0x00, 0x64, 0x00, 0x76, 0x00, 0xae, 0x00, 0xd4, 0x00, 0x2a, 0x01, 0x33, 0x01,
	0x35, 0x01, 0x4b, 0x01, 0x0d, 0x02, 0x3f, 0x02, 0x5b, 0x02, 0x6d, 0x02, 0x79, 0x02,
	0xa3, 0x02, 0xae, 0x02, 0x00, 0x00, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1b, 0x02, 0x00, 0x00,
	0x3e, 0x00, 0x7a, 0x00, 0xb6, 0x00, 0xcc, 0x00, 0xe3, 0x00, 0xfa, 0x00, 0x00, 0x00,
	0x3c, 0x00, 0x78, 0x00, 0xb4, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x78, 0x00,
	0xb4, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x78, 0x00, 0xb4, 0x00, 0xf0, 0x00,
	0x00, 0x00, 0x3c, 0x00, 0x78, 0x00, 0xb4, 0x00, 0xf0, 0x00, 0x09, 0x04, 0x44, 0x10,
	0xfc, 0x03, 0x45, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x0e, 0x08, 0x01, 0x3b, 0x00, 0x24, 0x00,
	0x58, 0x04, 0x64, 0x00, 0xc0, 0x06, 0x85, 0x09, 0xa0, 0x00, 0x3c, 0x00, 0x24, 0x00,
	0x00, 0x04, 0x50, 0x00, 0xd0, 0x01, 0x60, 0x13, 0xd0, 0x00, 0x3c, 0x00, 0x24, 0x00,
	0x00, 0x04, 0x50, 0x00, 0xd0, 0x01, 0x8c, 0x14, 0x08, 0x01, 0x3c, 0x00, 0x24, 0x00,
	0x00, 0x04, 0x50, 0x00, 0xd0, 0x01, 0xb8, 0x15, 0x08, 0x01, 0x3c, 0x00, 0x24, 0x00,
	0x00, 0x04, 0x50, 0x00, 0xd0, 0x01, 0x44, 0x16, 0xb5, 0x00, 0x52, 0x00, 0x24, 0x00,
	0x87, 0x04, 0x64, 0x00, 0x6e, 0x02, 0x85, 0x09, 0x01, 0x07, 0x10, 0x00, 0x00, 0x40,
	0x00, 0x00, 0x01, 0x00, 0x00, 0x05, 0x04, 0x00, 0x00, 0xfe, 0xfa, 0x00, 0x00, 0x00,
	0xff, 0xff, 0xff, 0xff, 0xfd, 0xfd, 0xfd, 0xfd, 0xfb, 0x00, 0xfd, 0xfa, 0xf7, 0x30,
	0x04, 0x0f, 0x00, 0x00, 0x0f, 0x0f, 0x00, 0x00, 0x0f, 0x0f, 0x00, 0x00, 0x0f, 0x0f,
	0x00, 0x00, 0x0f, 0x0f, 0x00, 0x00, 0x0f, 0x0f, 0x00, 0x00, 0x0f, 0x0f, 0x00, 0x00,
	0x0f, 0x0f, 0x00, 0x00, 0x0f, 0x0f, 0x00, 0x00, 0x0f, 0x0f, 0x00, 0x00, 0x0f, 0x0f,
	0x00, 0x00, 0x0f, 0x0f, 0x00, 0x00, 0x0f, 0x0f, 0x00, 0x00, 0x0f, 0x0f, 0x00, 0x00,
	0x0f, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f,
	0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00,
	0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f,
	0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00,
	0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f,
	0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00,
	0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f,
	0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00,
	0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f,
	0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x30, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x02, 0x94, 0x05, 0x94, 0x05, 0x94, 0x05, 0x94,
	0x05, 0x95, 0x05, 0x94, 0x05, 0x82, 0x00, 0x9b, 0x00, 0x9b, 0x00, 0x9b, 0x00, 0x9b,
	0x00, 0xc3, 0x00, 0x01, 0x00, 0x10, 0x01, 0x1e, 0x18, 0x18, 0x1e, 0x00, 0x22, 0x00,
	0x00, 0x24, 0x25, 0x26, 0x27, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x01, 0x20, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x05, 0x07, 0x05, 0x03, 0x00, 0x08,
	0x02, 0x02, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x0a, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

int main()
{
	DBusError error;
	DBusConnection *conn;
	struct nl_handle *nlh;
	unsigned char *nvs = NULL;
	unsigned long nvs_len = 0;
	unsigned char address[6];
	int country_code = 0;
	int fcc;
	char regdomain[3];

	wl1251_cal_read(address, &fcc, &nvs, &nvs_len);

	if (!nvs)
		wl1251_vfs_read_nvs(&nvs, &nvs_len);

	if (!nvs) {
		nvs = default_nvs;
		nvs_len = sizeof(default_nvs);
	}

	if (wl1251_vfs_read_regdomain(regdomain) < 0) {
		dbus_error_init(&error);
		conn = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
		if (!conn) {
			fprintf(stderr, "wl1251-cal: couldn't get dbus system bus. %s\n", error.message);
			dbus_error_free(&error);
		} else {
			country_code = wl1251_csd_read_contry_code(conn);
			if (!country_code)
				country_code = wl1251_ofono_read_country_code(conn);
		}
		dbus_connection_unref(conn);
		wl1251_country_code_to_regdomain(country_code, fcc, regdomain);
	}

	if (memcmp(regdomain, "US", 3) == 0 && nvs_len == 756) {
		nvs[377] = 2;
		nvs[337] = 2;
		nvs[380] = 9;
		nvs[340] = 9;
	}

	if (memcmp(address, "\0\0\0\0\0\0", 6) != 0)
		wl1251_set_mac_address("wlan0", address);

	nlh = wl1251_nl_connect();
	if (nlh) {
		if (wl1251_nl_push_nvs(nlh, "wlan0", nvs+4, nvs_len-4) < 0)
			fprintf(stderr, "wl1251-cal: Couldnt push NVS\n");
		wl1251_nl_receive(nlh);
		if (wl1251_nl_push_regdomain(nlh, regdomain) < 0)
			fprintf(stderr, "wl1251-cal: Couldnt push regdomain\n");
		wl1251_nl_receive(nlh);
		wl1251_nl_destroy(nlh);
	}

	return 0;
}
