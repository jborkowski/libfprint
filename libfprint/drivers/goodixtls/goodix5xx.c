// Goodix Tls driver for libfprint

// Copyright (C) 2021 Alexander Meiler <alex.meiler@protonmail.com>
// Copyright (C) 2021 Matthieu CHARETTE <matthieu.charette@gmail.com>
// Copyright (C) 2021 Natasha England-Elbro <ashenglandelbro@protonmail.com>

// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "drivers/goodixtls/goodix5xx.h"
#include "drivers_api.h"
#include "goodix.h"


typedef struct
{

} FpiDeviceGoodixTlsPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (FpiDeviceGoodixTls, fpi_device_goodixtls5xx, FPI_TYPE_DEVICE_GOODIXTLS5XX)

#pragma region "constants"

enum SCAN_STAGES {
  SCAN_STAGE_QUERY_MCU,
  SCAN_STAGE_SWITCH_TO_FDT_MODE,
  SCAN_STAGE_SWITCH_TO_FDT_DOWN,
  SCAN_STAGE_GET_IMG,
  SCAN_STAGE_SWITCH_TO_FTD_UP,
  SCAN_STAGE_SWITCH_TO_FTD_DONE,

  SCAN_STAGE_NUM,
};

#pragma endregion

#pragma region "check callbacks"

static void
check_none_cmd (FpDevice *dev, guint8 *data, guint16 len,
                gpointer ssm, GError *err)
{
  if (err)
    {
      fpi_ssm_mark_failed (ssm, err);
      return;
    }
  fpi_ssm_next_state (ssm);
}

#pragma endregion

#pragma region "scan"

static void
query_mcu_state_cb (FpDevice * dev, guchar * mcu_state, guint16 len,
                    gpointer ssm, GError * error)
{
  if (error)
    {
      fpi_ssm_mark_failed (ssm, error);
      return;
    }
  fpi_ssm_next_state (ssm);
}

static void
scan_on_read_img (FpDevice *dev, guint8 *data, guint16 len,
                  gpointer ssm, GError *err)
{
  if (err)
    {
      fpi_ssm_mark_failed (ssm, err);
      return;
    }

  FpiDeviceGoodixTls5xxClass *cls = FPI_DEVICE_GOODIXTLS5XX_GET_CLASS (dev);
  FpImageDevice * img_dev = FP_IMAGE_DEVICE (dev);

  GoodixTls5xxPix * raw_frame = malloc ((cls->scan_width * cls->scan_height) * sizeof (GoodixTls5xxPix));
  goodixtls5xx_decode_frame (raw_frame, cls->scan_height * cls->scan_width, data);
  FpImage * img = cls->process_frame (raw_frame);

  fpi_image_device_image_captured (img_dev, img);

  fpi_ssm_next_state (ssm);
}

static void
scan_get_img (FpDevice * dev, FpiSsm * ssm)
{
  goodix_tls_read_image (dev, scan_on_read_img, ssm);
}

static void
send_switch_mode (FpDevice * dev, gpointer ssm, void (*mode_switch)(FpDevice *, const guint8 *, guint16, GDestroyNotify, GoodixDefaultCallback, gpointer))
{
  FpiDeviceGoodixTls5xxClass *cls = FPI_DEVICE_GOODIXTLS5XX_GET_CLASS (dev);
  GoodixTls5xxMcuConfig cfg = cls->get_mcu_cfg ();

  mode_switch (dev, cfg.data, cfg.data_len, cfg.free_fn, check_none_cmd, ssm);
}

static void
scan_run_state (FpiSsm * ssm, FpDevice * dev)
{
  FpImageDevice *img_dev = FP_IMAGE_DEVICE (dev);
  FpiDeviceGoodixTls5xxClass *cls = FPI_DEVICE_GOODIXTLS5XX_GET_CLASS (dev);

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case SCAN_STAGE_QUERY_MCU:
      goodix_send_query_mcu_state (dev, query_mcu_state_cb, ssm);
      break;

    case SCAN_STAGE_SWITCH_TO_FDT_MODE:
      send_switch_mode (dev, ssm, goodix_send_mcu_switch_to_fdt_mode);
      break;

    case SCAN_STAGE_SWITCH_TO_FDT_DOWN:
      send_switch_mode (dev, ssm, goodix_send_mcu_switch_to_fdt_down );
      break;

    case SCAN_STAGE_GET_IMG:
      fpi_image_device_report_finger_status (img_dev, TRUE);
      scan_get_img (dev, ssm);
      break;

    case SCAN_STAGE_SWITCH_TO_FTD_UP:
      send_switch_mode (dev, ssm, goodix_send_mcu_switch_to_fdt_up );
      break;

    case SCAN_STAGE_SWITCH_TO_FTD_DONE:
      fpi_image_device_report_finger_status (img_dev, FALSE);
      break;
    }
}

static void
scan_complete (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  if (error)
    {
      fp_err ("failed to scan: %s (code: %d)", error->message, error->code);
      fpi_image_device_session_error (FP_IMAGE_DEVICE (dev), error);
      return;
    }
  fp_dbg ("finished scan");
}

#pragma endregion

void
goodixtls5xx_scan_start (FpDevice * dev)
{
  fpi_ssm_start (fpi_ssm_new (dev, scan_run_state, SCAN_STAGE_NUM), scan_complete);
}

void
goodixtls5xx_decode_frame (GoodixTls5xxPix * frame, guint32 frame_size, const guint8 *raw_frame)
{
  GoodixTls5xxPix *pix = frame;

  for (int i = 8; i != frame_size - 5; i += 6)
    {
      const guint8 *chunk = raw_frame + i;
      *pix++ = ((chunk[0] & 0xf) << 8) + chunk[1];
      *pix++ = (chunk[3] << 4) + (chunk[0] >> 4);
      *pix++ = ((chunk[5] & 0xf) << 8) + chunk[2];
      *pix++ = (chunk[4] << 4) + (chunk[5] >> 4);
    }
}
