package io.github.skyliner2008.pabflasher

import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import java.io.IOException

/**
 * USB CDC-ACM through Android's USB host API (no kernel driver, no root): the ESP32-S3's own USB port
 * (USB-Serial-JTAG, 303A:1001) is a CDC device - interface "comm" for the control lines, "data" with two bulk endpoints.
 */
class UsbCdcPort private constructor(
    private val manager: UsbManager,
    private val device: UsbDevice,
    private val conn: UsbDeviceConnection,
    private val claimed: List<UsbInterface>,
    private val controlIndex: Int,
    private val epIn: UsbEndpoint,
    private val epOut: UsbEndpoint,
) : SerialPort {

    override fun write(data: ByteArray, timeoutMs: Int) {
        var off = 0
        while (off < data.size) {
            val n = conn.bulkTransfer(epOut, data, off, minOf(16 * 1024, data.size - off), timeoutMs)
            if (n <= 0) {
                if (!attached()) throw IOException("สายหลุด / บอร์ดหายไปจาก USB")
                throw IOException("เขียนข้อมูลไปที่บอร์ดไม่ได้")
            }
            off += n
        }
    }

    override fun read(buf: ByteArray, timeoutMs: Int): Int {
        val n = conn.bulkTransfer(epIn, buf, buf.size, timeoutMs)
        if (n > 0) return n
        if (!attached()) throw IOException("สายหลุด / บอร์ดหายไปจาก USB")
        return 0
    }

    override fun setLines(dtr: Boolean, rts: Boolean) {
        val value = (if (dtr) 1 else 0) or (if (rts) 2 else 0)
        if (conn.controlTransfer(0x21, SET_CONTROL_LINE_STATE, value, controlIndex, null, 0, 1000) < 0 && !attached()) {
            throw IOException("สายหลุด / บอร์ดหายไปจาก USB")
        }
    }

    override fun close() {
        claimed.forEach { conn.releaseInterface(it) }
        conn.close()
    }

    private fun attached() = manager.deviceList.values.any { it.deviceName == device.deviceName }

    companion object {
        const val ESPRESSIF_VID = 0x303A
        private const val SET_LINE_CODING = 0x20
        private const val SET_CONTROL_LINE_STATE = 0x22

        fun findBoard(manager: UsbManager): UsbDevice? =
            manager.deviceList.values.firstOrNull { it.vendorId == ESPRESSIF_VID }

        fun open(manager: UsbManager, device: UsbDevice): UsbCdcPort {
            var comm: UsbInterface? = null
            var data: UsbInterface? = null
            for (i in 0 until device.interfaceCount) {
                val itf = device.getInterface(i)
                when (itf.interfaceClass) {
                    UsbConstants.USB_CLASS_COMM -> if (comm == null) comm = itf
                    UsbConstants.USB_CLASS_CDC_DATA -> if (data == null) data = itf
                }
            }
            data ?: throw IOException("อุปกรณ์นี้ไม่ใช่พอร์ต USB serial (CDC)")
            var epIn: UsbEndpoint? = null
            var epOut: UsbEndpoint? = null
            for (i in 0 until data.endpointCount) {
                val ep = data.getEndpoint(i)
                if (ep.type != UsbConstants.USB_ENDPOINT_XFER_BULK) continue
                if (ep.direction == UsbConstants.USB_DIR_IN) epIn = ep else epOut = ep
            }
            if (epIn == null || epOut == null) throw IOException("ไม่พบช่องรับส่งข้อมูลของบอร์ด")
            val conn = manager.openDevice(device) ?: throw IOException("เปิดพอร์ต USB ไม่ได้ (ยังไม่ได้อนุญาต?)")
            val claimed = listOfNotNull(comm, data)
            for (itf in claimed) {
                if (!conn.claimInterface(itf, true)) {
                    conn.close()
                    throw IOException("แอปอื่นใช้พอร์ตนี้อยู่")
                }
            }
            val ctrl = comm?.id ?: 0
            // 115200 8N1: the USB-Serial-JTAG ignores it, other CDC bridges want one
            val coding = byteArrayOf(0x00, 0xC2.toByte(), 0x01, 0x00, 0, 0, 8)
            conn.controlTransfer(0x21, SET_LINE_CODING, 0, ctrl, coding, coding.size, 1000)
            return UsbCdcPort(manager, device, conn, claimed, ctrl, epIn, epOut)
        }
    }
}
