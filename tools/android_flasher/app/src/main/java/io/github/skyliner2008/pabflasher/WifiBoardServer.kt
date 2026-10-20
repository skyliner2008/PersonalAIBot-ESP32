package io.github.skyliner2008.pabflasher

import java.io.BufferedReader
import java.io.IOException
import java.io.InputStreamReader
import java.io.OutputStream
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.NetworkInterface
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketTimeoutException
import java.util.concurrent.Executors

/**
 * The phone side of the Wi-Fi link (wiki 18): the board connects here (TCP 47800). In a car the board rides
 * the phone's hotspot, so it finds us at its gateway; on a shared Wi-Fi we say where we are on UDP 47801.
 * One board at a time; nothing is accepted from it before it proved the pairing token (and we proved ours).
 */
class WifiBoardServer(
    private val token: () -> String,
    private val app: String,
    private val listener: Listener,
    private val port: Int = PhoneProtocol.TCP_PORT,
    private val announcePort: Int = PhoneProtocol.UDP_PORT,
) {
    interface Listener {
        fun onBoardConnected(address: String)
        fun onBoardRequest(json: String)
        fun onBoardGone(why: String)
    }

    @Volatile private var running = false
    @Volatile private var server: ServerSocket? = null
    @Volatile private var active: Socket? = null
    @Volatile private var out: OutputStream? = null
    private val writeLock = Any()
    private val writer = Executors.newSingleThreadExecutor()  // never the caller's thread: GPS comes on the UI thread

    val connected get() = out != null
    val boundPort get() = server?.localPort ?: -1

    fun start() {
        if (running) return
        running = true
        val s = ServerSocket()
        s.reuseAddress = true
        s.bind(InetSocketAddress(port))
        server = s
        Thread({ acceptLoop(s) }, "wifi-accept").apply { isDaemon = true }.start()
        if (announcePort > 0) Thread({ announceLoop() }, "wifi-announce").apply { isDaemon = true }.start()
    }

    fun stop() {
        running = false
        try { server?.close() } catch (_: IOException) {}
        server = null
        drop(active, "stopped")
    }

    /** One "phone {...}" line to the board (queued, in order); false = no board on Wi-Fi. */
    fun send(line: String): Boolean {
        if (out == null) return false
        writer.execute {
            val o = out ?: return@execute
            try {
                synchronized(writeLock) {
                    o.write((line + "\n").toByteArray(Charsets.UTF_8))
                    o.flush()
                }
            } catch (e: IOException) {
                drop(active, "send failed: ${e.message}")
            }
        }
        return true
    }

    private fun acceptLoop(s: ServerSocket) {
        while (running) {
            val sock = try { s.accept() } catch (_: IOException) { break }
            Thread({ serve(sock) }, "wifi-board").apply { isDaemon = true }.start()
        }
    }

    private fun serve(sock: Socket) {
        val addr = sock.inetAddress?.hostAddress ?: "?"
        try {
            sock.tcpNoDelay = true
            sock.soTimeout = 5000  // the handshake must be quick
            val reader = BufferedReader(InputStreamReader(sock.getInputStream(), Charsets.UTF_8))
            val o = sock.getOutputStream()
            val tok = token()
            val hello = reader.readLine() ?: throw IOException("closed")
            if (!hello.startsWith("hello ")) throw IOException("not a board")
            val boardNonce = org.json.JSONObject(hello.substring(6)).optString("nonce")
            if (!PhoneProtocol.validNonce(boardNonce) || tok.isEmpty()) throw IOException("bad hello")
            val mine = PhoneProtocol.newNonce()
            o.write((PhoneProtocol.hello(app, mine, PhoneProtocol.proof(tok, "phone", boardNonce)) + "\n").toByteArray())
            o.flush()
            val auth = reader.readLine() ?: throw IOException("closed during the handshake")
            val proof = if (auth.startsWith("auth ")) org.json.JSONObject(auth.substring(5)).optString("proof") else ""
            if (!PhoneProtocol.proofOk(tok, "board", mine, proof)) throw IOException("the board did not prove the pairing")
            sock.soTimeout = 0  // the board talks only when it asks for something
            drop(active, "replaced by a new connection")
            synchronized(writeLock) {
                active = sock
                out = o
            }
            listener.onBoardConnected(addr)
            while (running) {
                val line = reader.readLine() ?: break
                if (line.startsWith("PHONE ")) listener.onBoardRequest(line.substring(6))
            }
            drop(sock, "board closed the connection")
        } catch (e: SocketTimeoutException) {
            drop(sock, "board too slow")
        } catch (e: Exception) {
            if (active === sock) drop(sock, e.message ?: "error")
            else try { sock.close() } catch (_: IOException) {}
            if (e.message?.contains("prove") == true) listener.onBoardGone("$addr: ${e.message}")
        }
    }

    private fun drop(sock: Socket?, why: String) {
        if (sock == null) return
        var was = false
        synchronized(writeLock) {
            if (active === sock) {
                active = null
                out = null
                was = true
            }
        }
        try { sock.close() } catch (_: IOException) {}
        if (was) listener.onBoardGone(why)
    }

    /** "PABPHONE 47800" every 3 s to every broadcast address we have (a board on a shared Wi-Fi). */
    private fun announceLoop() {
        val payload = "PABPHONE $port".toByteArray()
        try {
            DatagramSocket().use { ds ->
                ds.broadcast = true
                while (running) {
                    val targets = LinkedHashSet<InetAddress>()
                    try {
                        for (nif in NetworkInterface.getNetworkInterfaces()) {
                            if (!nif.isUp || nif.isLoopback) continue
                            for (a in nif.interfaceAddresses) a.broadcast?.let { targets += it }
                        }
                    } catch (_: Exception) {
                    }
                    targets += InetAddress.getByName("255.255.255.255")
                    for (t in targets) {
                        try { ds.send(DatagramPacket(payload, payload.size, t, announcePort)) } catch (_: IOException) {}
                    }
                    Thread.sleep(3000)
                }
            }
        } catch (_: Exception) {
        }
    }
}
