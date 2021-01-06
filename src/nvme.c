/*
 * Copyright (C) 2020 Constantine Gavrilov <constantine.gavrilov@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "hddtemp.h"
#include <sys/ioctl.h>
#include <linux/nvme_ioctl.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

struct nvme_smart_log {
  unsigned char  critical_warning;
  unsigned char  temperature[2];
  unsigned char  avail_spare;
  unsigned char  spare_thresh;
  unsigned char  percent_used;
  unsigned char  rsvd6[26];
  unsigned char  data_units_read[16];
  unsigned char  data_units_written[16];
  unsigned char  host_reads[16];
  unsigned char  host_writes[16];
  unsigned char  ctrl_busy_time[16];
  unsigned char  power_cycles[16];
  unsigned char  power_on_hours[16];
  unsigned char  unsafe_shutdowns[16];
  unsigned char  media_errors[16];
  unsigned char  num_err_log_entries[16];
  unsigned int   warning_temp_time;
  unsigned int   critical_comp_time;
  unsigned short temp_sensor[8];
  unsigned int   thm_temp1_trans_count;
  unsigned int   thm_temp2_trans_count;
  unsigned int   thm_temp1_total_time;
  unsigned int   thm_temp2_total_time;
  unsigned char  rsvd232[280];
};

struct nvme_id_power_state {
  unsigned short  max_power; // centiwatts
  unsigned char   rsvd2;
  unsigned char   flags;
  unsigned int    entry_lat; // microseconds
  unsigned int    exit_lat;  // microseconds
  unsigned char   read_tput;
  unsigned char   read_lat;
  unsigned char   write_tput;
  unsigned char   write_lat;
  unsigned short  idle_power;
  unsigned char   idle_scale;
  unsigned char   rsvd19;
  unsigned short  active_power;
  unsigned char   active_work_scale;
  unsigned char   rsvd23[9];
};

struct nvme_id_ctrl {
  unsigned short  vid;
  unsigned short  ssvid;
  char            sn[20];
  char            mn[40];
  char            fr[8];
  unsigned char   rab;
  unsigned char   ieee[3];
  unsigned char   cmic;
  unsigned char   mdts;
  unsigned short  cntlid;
  unsigned int    ver;
  unsigned int    rtd3r;
  unsigned int    rtd3e;
  unsigned int    oaes;
  unsigned int    ctratt;
  unsigned char   rsvd100[156];
  unsigned short  oacs;
  unsigned char   acl;
  unsigned char   aerl;
  unsigned char   frmw;
  unsigned char   lpa;
  unsigned char   elpe;
  unsigned char   npss;
  unsigned char   avscc;
  unsigned char   apsta;
  unsigned short  wctemp;
  unsigned short  cctemp;
  unsigned short  mtfa;
  unsigned int    hmpre;
  unsigned int    hmmin;
  unsigned char   tnvmcap[16];
  unsigned char   unvmcap[16];
  unsigned int    rpmbs;
  unsigned short  edstt;
  unsigned char   dsto;
  unsigned char   fwug;
  unsigned short  kas;
  unsigned short  hctma;
  unsigned short  mntmt;
  unsigned short  mxtmt;
  unsigned int    sanicap;
  unsigned char   rsvd332[180];
  unsigned char   sqes;
  unsigned char   cqes;
  unsigned short  maxcmd;
  unsigned int    nn;
  unsigned short  oncs;
  unsigned short  fuses;
  unsigned char   fna;
  unsigned char   vwc;
  unsigned short  awun;
  unsigned short  awupf;
  unsigned char   nvscc;
  unsigned char   rsvd531;
  unsigned short  acwu;
  unsigned char   rsvd534[2];
  unsigned int    sgls;
  unsigned char   rsvd540[228];
  char            subnqn[256];
  unsigned char   rsvd1024[768];
  unsigned int    ioccsz;
  unsigned int    iorcsz;
  unsigned short  icdoff;
  unsigned char   ctrattr;
  unsigned char   msdbd;
  unsigned char   rsvd1804[244];
  struct nvme_id_power_state  psd[32];
  unsigned char   vs[1024];
};

#include <stdio.h>

static int nvme_probe(int fd)
{
  return (ioctl(fd, NVME_IOCTL_ID, NULL) > 0);
}

static bool nvme_read_smart_log(int fd, struct nvme_smart_log *smart_log)
{
  unsigned int size = sizeof(*smart_log);
  struct nvme_passthru_cmd pt = { 0 };

  memset(smart_log, 0, size);
  pt.opcode = 0x02;
  pt.nsid = 0xffffffff;
  pt.addr = (uint64_t)smart_log;
  pt.data_len = size;
  pt.cdw10 = 0x02 | (((size / 4) - 1) << 16);
  if (ioctl(fd, NVME_IOCTL_ADMIN_CMD, &pt) < 0)
    return false;
  return true;
}

static bool nvme_read_id_ctrl(int fd, struct nvme_id_ctrl *id)
{
  memset(id, 0, sizeof(*id));
  struct nvme_passthru_cmd pt = { 0 };
  pt.opcode = 0x06;
  pt.nsid = 0;
  pt.addr = (uint64_t)id;
  pt.data_len = sizeof(*id);
  pt.cdw10 = 0x01;
  if (ioctl(fd, NVME_IOCTL_ADMIN_CMD, &pt) < 0)
    return false;
  return true;
}


const char *nvme_model(int fd)
{
  struct nvme_id_ctrl id;
  unsigned int i;
  char *p;
  const unsigned int name_len = sizeof(id.mn);

  if (nvme_read_id_ctrl(fd, &id) == false)
    return "NVME Disk";
  id.mn[name_len-1] = '\0';
  for (i = name_len - 2; i > 0; i--) {
    if (id.mn[i] == ' ')
      id.mn[i] = '\0';
    else
      break;
  }
  p = id.mn;
  for (i = 0; i < name_len; i++) {
    if (id.mn[i] == ' ')
      id.mn[i] = '\0';
    else
      break;
  }
  p = strdup(p);
  if (!p || strlen(p) == 0)
      return "NVME Disk";
  for (i = 0; p[i]; i++) {
    if (p[i] < 0x20 || p[i] > 0x7e)
      p[i] = '?';
  }
  return p;
}

enum e_gettemp nvme_get_temperature(struct disk *disk)
{
  struct nvme_smart_log smart_log;
  if (nvme_read_smart_log(disk->fd, &smart_log) == false)
    return GETTEMP_UNKNOWN;
  disk->value = smart_log.temperature[0] + (smart_log.temperature[1] << 8) - 273;
  return GETTEMP_KNOWN;
}
  
struct bustype nvme_bus = {
  "NVME",
  nvme_probe,
  nvme_model,
  nvme_get_temperature
};


