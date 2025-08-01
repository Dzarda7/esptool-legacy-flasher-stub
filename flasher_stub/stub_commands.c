/*
 * SPDX-FileCopyrightText: 2019-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdlib.h>
#include "stub_commands.h"
#include "stub_write_flash.h"
#include "stub_flasher.h"
#include "rom_functions.h"
#include "slip.h"
#include "soc_support.h"
#include "stub_io.h"

#if (ESP32S3 && !ESP32S3BETA2) || ESP32P4 || ESP32C5
static esp_rom_spiflash_result_t SPIRead4B(int spi_num, uint32_t flash_addr, uint8_t* buf, int len)
{
    uint8_t cmd_len = 8;

    esp_rom_spiflash_wait_idle();
    while (len > 0) {
        int rd_length;
        if (len > 16 ) {    //16 = read_sub_len
            rd_length = 16;
        } else {
            rd_length = len;
        }
        esp_rom_opiflash_exec_cmd(spi_num, SPI_FLASH_FASTRD_MODE,
                                CMD_FSTRD4B, cmd_len,
                                flash_addr, 32,
                                8,
                                NULL, 0,
                                buf, 8 * rd_length,
                                ESP_ROM_OPIFLASH_SEL_CS0,
                                false);

        len -= rd_length;
        buf += rd_length;
        flash_addr += rd_length;
    }
    return ESP_ROM_SPIFLASH_RESULT_OK;
}
#endif // ESP32S3 || ESP32P4 || ESP32C5

int handle_flash_erase(uint32_t addr, uint32_t len) {
  if (addr % FLASH_SECTOR_SIZE != 0) return 0x32;
  if (len % FLASH_SECTOR_SIZE != 0) return 0x33;
  if (SPIUnlock() != 0) return 0x34;

  while (len > 0 && (addr % FLASH_BLOCK_SIZE != 0)) {
    #if defined(ESP32S3) && !defined(ESP32S3BETA2)
        if (large_flash_mode) {
          if (esp_rom_opiflash_erase_sector(addr / FLASH_SECTOR_SIZE) != 0) return 0x35;
        } else {
          if (SPIEraseSector(addr / FLASH_SECTOR_SIZE) != 0) return 0x35;
        }
    #elif ESP32P4 || ESP32C5
        if (large_flash_mode) {
          spi_write_enable();
          esp_rom_spiflash_wait_idle();
          esp_rom_opiflash_exec_cmd(1, SPI_FLASH_SLOWRD_MODE,
            CMD_SECTOR_ERASE_4B, 8,
            addr, 32,
            0,
            NULL, 0,
            NULL, 0,
            1,
            true);
        } else {
          if (SPIEraseSector(addr / FLASH_SECTOR_SIZE) != 0) return 0x35;
        }
    #else
      if (SPIEraseSector(addr / FLASH_SECTOR_SIZE) != 0) return 0x35;
    #endif // ESP32S3
    len -= FLASH_SECTOR_SIZE;
    addr += FLASH_SECTOR_SIZE;
  }

  while (len > FLASH_BLOCK_SIZE) {
    #if defined(ESP32S3) && !defined(ESP32S3BETA2)
      if (large_flash_mode) {
        if (esp_rom_opiflash_erase_block_64k(addr / FLASH_BLOCK_SIZE) != 0) return 0x36;
      } else {
        if (SPIEraseBlock(addr / FLASH_BLOCK_SIZE) != 0) return 0x36;
      }
    #elif ESP32P4 || ESP32C5
      if (large_flash_mode) {
        spi_write_enable();
        esp_rom_spiflash_wait_idle();
        esp_rom_opiflash_exec_cmd(1, SPI_FLASH_SLOWRD_MODE,
          CMD_LARGE_BLOCK_ERASE_4B, 8,
          addr, 32,
          0,
          NULL, 0,
          NULL, 0,
          1,
          true);
    } else {
      if (SPIEraseBlock(addr / FLASH_BLOCK_SIZE) != 0) return 0x36;
    }
    #else
      if (SPIEraseBlock(addr / FLASH_BLOCK_SIZE) != 0) return 0x36;
    #endif // ESP32S3
    len -= FLASH_BLOCK_SIZE;
    addr += FLASH_BLOCK_SIZE;
  }

  while (len > 0) {
    #if defined(ESP32S3) && !defined(ESP32S3BETA2)
      if (large_flash_mode) {
        if (esp_rom_opiflash_erase_sector(addr / FLASH_SECTOR_SIZE) != 0) return 0x37;
      } else {
        if (SPIEraseSector(addr / FLASH_SECTOR_SIZE) != 0) return 0x37;
      }
    #elif ESP32P4 || ESP32C5
      if (large_flash_mode) {
        spi_write_enable();
        esp_rom_spiflash_wait_idle();
        esp_rom_opiflash_exec_cmd(1, SPI_FLASH_SLOWRD_MODE,
          CMD_SECTOR_ERASE_4B, 8,
          addr, 32,
          0,
          NULL, 0,
          NULL, 0,
          1,
          true);
      } else {
        if (SPIEraseSector(addr / FLASH_SECTOR_SIZE) != 0) return 0x35;
      }
    #else
      if (SPIEraseSector(addr / FLASH_SECTOR_SIZE) != 0) return 0x37;
    #endif // ESP32S3
    len -= FLASH_SECTOR_SIZE;
    addr += FLASH_SECTOR_SIZE;
  }

  return 0;
}

void handle_flash_read(uint32_t addr, uint32_t len, uint32_t block_size,
                  uint32_t max_in_flight) {
  uint8_t buf[FLASH_SECTOR_SIZE];
  uint8_t digest[16];
  struct MD5Context ctx;
  uint32_t num_sent = 0, num_acked = 0;
  uint8_t res = 0;

  /* This is one routine where we still do synchronous I/O */
  stub_rx_async_enable(false);

  if (block_size > sizeof(buf)) {
    return;
  }
  MD5Init(&ctx);
  while (num_acked < len && num_acked <= num_sent) {
    while (num_sent < len && num_sent - num_acked < max_in_flight) {
      uint32_t n = len - num_sent;
      if (n > block_size) n = block_size;
      #if (ESP32S3 && !ESP32S3BETA2) || ESP32P4 || ESP32C5
        if (large_flash_mode) {
          res = SPIRead4B(1, addr, buf, n);
        } else {
          res = SPIRead(addr, (uint32_t *)buf, n);
        }
      #else
        res = SPIRead(addr, (uint32_t *)buf, n);
      #endif // ESP32S3
      if (res != 0) {
        break;
      }
      SLIP_send(buf, n);
      MD5Update(&ctx, buf, n);
      addr += n;
      num_sent += n;
    }
    int r = SLIP_recv(&num_acked, sizeof(num_acked));
    if (r != 4) {
      break;
    }
  }
  MD5Final(digest, &ctx);
  SLIP_send(digest, sizeof(digest));

  /* Go back to async RX */
  stub_rx_async_enable(true);
}

int handle_flash_get_md5sum(uint32_t addr, uint32_t len) {
  uint8_t buf[FLASH_SECTOR_SIZE];
  uint8_t digest[16];
  uint8_t res = 0;
  struct MD5Context ctx;
  MD5Init(&ctx);
  while (len > 0) {
    uint32_t n = len;
    if (n > FLASH_SECTOR_SIZE) {
      n = FLASH_SECTOR_SIZE;
    }
    #if (ESP32S3 && !ESP32S3BETA2) || ESP32P4 || ESP32C5
      if (large_flash_mode) {
        res = SPIRead4B(1, addr, buf, n);
      } else {
        res = SPIRead(addr, (uint32_t *)buf, n);
      }
    #else
      res = SPIRead(addr, (uint32_t *)buf, n);
    #endif // ESP32S3
    if (res != 0) {
      return 0x63;
    }
    MD5Update(&ctx, buf, n);
    addr += n;
    len -= n;
  }
  MD5Final(digest, &ctx);
  /* ESP32 ROM sends as hex, but we just send raw bytes - esptool.py can handle either. */
  SLIP_send_frame_data_buf(digest, sizeof(digest));
  return 0;
}

esp_command_error handle_spi_set_params(uint32_t *args, int *status)
{
  *status = SPIParamCfg(args[0], args[1], args[2], args[3], args[4], args[5]);
  return *status ? ESP_FAILED_SPI_OP : ESP_OK;
}

esp_command_error handle_spi_attach(uint32_t hspi_config_arg)
{
#ifdef ESP8266
        /* ESP8266 doesn't yet support SPI flash on HSPI, but could:
         see https://github.com/themadinventor/esptool/issues/98 */
        SelectSpiFunction();
#else
        /* Stub calls spi_flash_attach automatically when it boots,
          therefore, we need to "unattach" the flash before attaching again
          with different configuration to avoid issues. */

        // Configure the SPI flash pins back as classic GPIOs
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_SPICLK_U, FUNC_GPIO);
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_SPIQ_U, FUNC_GPIO);
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_SPID_U, FUNC_GPIO);
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_SPICS0_U, FUNC_GPIO);

        /* spi_flash_attach calls SelectSpiFunction() and another
           function to initialise SPI flash interface.

           Second argument 'legacy' mode is not currently supported.
        */
        spi_flash_attach(hspi_config_arg, 0);
#endif
        return ESP_OK; /* neither function/attach command takes an arg */
}

static uint32_t *mem_offset;
static uint32_t mem_remaining;

esp_command_error handle_mem_begin(uint32_t size, uint32_t offset)
{
    mem_offset = (uint32_t *)offset;
    mem_remaining = size;
    return ESP_OK;
}

esp_command_error handle_mem_data(void *data, uint32_t length)
{
    uint32_t *data_words = (uint32_t *)data;
    if (mem_offset == NULL && length > 0) {
        return ESP_NOT_IN_FLASH_MODE;
    }
    if (length > mem_remaining) {
        return ESP_TOO_MUCH_DATA;
    }
    if (length % 4 != 0) {
        return ESP_BAD_DATA_LEN;
    }

    for(int i = 0; i < length; i+= 4) {
        *mem_offset++ = *data_words++;
        mem_remaining -= 4;
    }
    return ESP_OK;
}

esp_command_error handle_mem_finish()
{
    esp_command_error res = mem_remaining > 0 ? ESP_NOT_ENOUGH_DATA : ESP_OK;
    mem_remaining = 0;
    mem_offset = NULL;
    return res;
}

esp_command_error handle_write_reg(const write_reg_args_t *cmds, uint32_t num_commands)
{
    for (uint32_t i = 0; i < num_commands; i++) {
        const write_reg_args_t *cmd = &cmds[i];
        ets_delay_us(cmd->delay_us);
        uint32_t v = cmd->value & cmd->mask;
        if (cmd->mask != UINT32_MAX) {
            v |= READ_REG(cmd->addr) & ~cmd->mask;
        }
        WRITE_REG(cmd->addr, v);
    }
    return ESP_OK;
}

#if ESP32S2_OR_LATER
esp_command_error handle_get_security_info()
{
  uint8_t buf[SECURITY_INFO_BYTES];
  esp_command_error ret;

  #ifdef ESP32C3
  if (_rom_eco_version >= 7)
    ret = GetSecurityInfoProcNewEco(NULL, NULL, buf);
  else
    ret = GetSecurityInfoProc(NULL, NULL, buf);
  #else
  ret = GetSecurityInfoProc(NULL, NULL, buf);
  #endif // ESP32C3
  if (ret == ESP_OK)
    SLIP_send_frame_data_buf(buf, sizeof(buf));
  return ret;
}
#endif // ESP32S2_OR_LATER
