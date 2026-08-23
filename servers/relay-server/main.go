package main

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"crypto/tls"
	"encoding/binary"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

const (
	helloMagic     = "RLY1"
	serverHelloLen = 12
	clientHelloLen = 4 + 1 + 2 + 16 + 32 + 32
	authReplyLen   = 4 + 1 + 1

	authStatusOK           = 0
	authStatusBadKey       = 1
	authStatusRoomMismatch = 2
	authStatusRoomFull     = 3

	waPacketLen = 112
	waMagic     = 0x41574C43
	waVersion   = 22

	waTypeAnnounce   = 1
	waTypeDiscover   = 2
	waTypeData       = 5
	waTypeDisconnect = 6
	waTypeChildData  = 10

	defaultOutQueue = 256
	maxRoomClients  = 4
)

var (
	serverKey   = flag.String("server-key", "", "relay server key (required)")
	listenAddr  = flag.String("listen", ":57318", "TCP listen address")
	adminListen = flag.String("admin-listen", "127.0.0.1:57321", "admin HTTP listen address (empty disables)")
	adminToken  = flag.String("admin-token", "", "optional admin bearer token")
	tlsCert     = flag.String("tls-cert", "", "TLS certificate file (with -tls-key enables TLS)")
	tlsKeyFile  = flag.String("tls-key", "", "TLS private key file (with -tls-cert enables TLS)")
	debug       = flag.Bool("debug", false, "log every routed packet")
	maxRooms    = flag.Int("max-rooms", 256, "maximum active rooms")
	maxClients  = flag.Int("max-clients", 64, "maximum authenticated clients")
)

type serverStats struct {
	authFailures   atomic.Uint64
	badKey         atomic.Uint64
	badRoom        atomic.Uint64
	droppedUnknown atomic.Uint64
	droppedQueue   atomic.Uint64
	routedPackets  atomic.Uint64
	clients        atomic.Int64
}

type client struct {
	dev       uint16
	room      string
	conn      net.Conn
	out       chan []byte
	done      chan struct{}
	outMu     sync.Mutex
	closed    bool
	closeOnce sync.Once
}

type relayRoom struct {
	number   string
	roomAuth []byte
	clients  map[uint16]*client
	hostAnn  map[uint16][]byte
}

type relayServer struct {
	key     string
	keyMu   sync.RWMutex
	mu      sync.Mutex
	rooms   map[string]*relayRoom
	stats   serverStats
	started time.Time
}

func main() {
	flag.Parse()
	if *serverKey == "" {
		fmt.Fprintln(os.Stderr, "-server-key is required")
		os.Exit(2)
	}
	srv := &relayServer{
		key:     *serverKey,
		rooms:   make(map[string]*relayRoom),
		started: time.Now(),
	}
	var ln net.Listener
	if *tlsCert != "" && *tlsKeyFile != "" {
		cert, certErr := tls.LoadX509KeyPair(*tlsCert, *tlsKeyFile)
		if certErr != nil {
			log.Fatalf("load tls keypair: %v", certErr)
		}
		tlsLn, listenErr := tls.Listen("tcp", *listenAddr, &tls.Config{
			Certificates: []tls.Certificate{cert},
			MinVersion:   tls.VersionTLS12,
		})
		if listenErr != nil {
			log.Fatalf("listen %s: %v", *listenAddr, listenErr)
		}
		ln = tlsLn
		log.Printf("relay TLS listening on %s", *listenAddr)
	} else {
		var listenErr error
		ln, listenErr = net.Listen("tcp", *listenAddr)
		if listenErr != nil {
			log.Fatalf("listen %s: %v", *listenAddr, listenErr)
		}
		log.Printf("relay listening on %s", *listenAddr)
	}

	if *adminListen != "" {
		go srv.serveAdmin()
	}

	for {
		conn, err := ln.Accept()
		if err != nil {
			log.Printf("accept: %v", err)
			continue
		}
		if int(srv.stats.clients.Load()) >= *maxClients {
			conn.Close()
			continue
		}
		go srv.serveConn(conn)
	}
}

func (s *relayServer) serveConn(conn net.Conn) {
	remote := conn.RemoteAddr().String()
	log.Printf("relay client connected addr=%s", remote)
	defer conn.Close()
	conn.SetDeadline(time.Now().Add(15 * time.Second))

	nonce := make([]byte, 8)
	if _, err := rand.Read(nonce); err != nil {
		return
	}
	hello := make([]byte, serverHelloLen)
	copy(hello[0:4], helloMagic)
	copy(hello[4:12], nonce)
	if _, err := conn.Write(hello); err != nil {
		return
	}

	clientHello := make([]byte, clientHelloLen)
	if _, err := io.ReadFull(conn, clientHello); err != nil {
		log.Printf("relay client hello read failed addr=%s err=%v", remote, err)
		return
	}
	if string(clientHello[0:4]) != helloMagic || clientHello[4] != 1 {
		s.stats.authFailures.Add(1)
		log.Printf("relay client bad hello addr=%s magic=%q version=%d",
			remote, string(clientHello[0:4]), clientHello[4])
		return
	}
	dev := binary.LittleEndian.Uint16(clientHello[5:7])
	room := strings.TrimRight(string(clientHello[7:23]), "\x00")
	if room == "" {
		s.stats.authFailures.Add(1)
		log.Printf("relay client empty room addr=%s dev=%d", remote, dev)
		return
	}
	clientAuth := clientHello[23:55]
	roomAuth := clientHello[55:87]

	expectedAuth := s.computeAuth(nonce, dev, room)
	if !hmac.Equal(clientAuth, expectedAuth) {
		s.stats.badKey.Add(1)
		s.stats.authFailures.Add(1)
		log.Printf("relay client bad server key addr=%s dev=%d room=%q",
			remote, dev, room)
		s.writeAuthReply(conn, authStatusBadKey)
		return
	}

	cl := &client{
		dev:  dev,
		room: room,
		conn: conn,
		out:  make(chan []byte, defaultOutQueue),
		done: make(chan struct{}),
	}

	if !s.addClient(room, roomAuth, cl) {
		s.stats.badRoom.Add(1)
		s.stats.authFailures.Add(1)
		log.Printf("relay client room auth mismatch/room full addr=%s dev=%d room=%q",
			remote, dev, room)
		s.writeAuthReply(conn, authStatusRoomMismatch)
		return
	}
	defer s.removeClient(cl)

	s.writeAuthReply(conn, authStatusOK)
	log.Printf("relay client authenticated addr=%s dev=%d room=%q",
		remote, dev, room)
	s.stats.clients.Add(1)
	defer s.stats.clients.Add(-1)

	conn.SetDeadline(time.Time{})
	go s.writerLoop(cl)
	s.readLoop(cl)
}

func (s *relayServer) currentKey() string {
	s.keyMu.RLock()
	defer s.keyMu.RUnlock()
	return s.key
}

func (s *relayServer) computeAuth(nonce []byte, dev uint16, room string) []byte {
	key := s.currentKey()
	mac := hmac.New(sha256.New, []byte(key))
	mac.Write(nonce)
	var devBuf [2]byte
	binary.LittleEndian.PutUint16(devBuf[:], dev)
	mac.Write(devBuf[:])
	mac.Write([]byte(room))
	return mac.Sum(nil)
}

func (s *relayServer) addClient(room string, roomAuth []byte, cl *client) bool {
	s.mu.Lock()
	defer s.mu.Unlock()

	r := s.rooms[room]
	if r == nil {
		if len(s.rooms) >= *maxRooms {
			return false
		}
		if len(roomAuth) != sha256.Size {
			return false
		}
		r = &relayRoom{
			number:   room,
			roomAuth: append([]byte(nil), roomAuth...),
			clients:  make(map[uint16]*client),
			hostAnn:  make(map[uint16][]byte),
		}
		s.rooms[room] = r
		log.Printf("relay room created room=%q first_dev=%d", room, cl.dev)
	} else if !hmac.Equal(r.roomAuth, roomAuth) {
		return false
	}
	if len(r.clients) >= maxRoomClients {
		return false
	}
	if _, exists := r.clients[cl.dev]; exists {
		return false
	}
	r.clients[cl.dev] = cl
	return true
}

func (s *relayServer) closeClient(cl *client) {
	if cl == nil {
		return
	}
	cl.closeOnce.Do(func() {
		close(cl.done)
		cl.outMu.Lock()
		if !cl.closed {
			cl.closed = true
		}
		close(cl.out)
		cl.conn.Close()
		cl.outMu.Unlock()
	})
}

func (s *relayServer) disconnectClientLocked(cl *client) {
	s.closeClient(cl)
	if r := s.rooms[cl.room]; r != nil {
		delete(r.clients, cl.dev)
		delete(r.hostAnn, cl.dev)
		log.Printf("relay client left dev=%d room=%q remaining=%d",
			cl.dev, cl.room, len(r.clients))
		if len(r.clients) == 0 {
			delete(s.rooms, cl.room)
			log.Printf("relay room destroyed room=%q", cl.room)
		}
	}
}

func (s *relayServer) removeClient(cl *client) {
	s.mu.Lock()
	s.disconnectClientLocked(cl)
	s.mu.Unlock()
}

type adminStatsView struct {
	AuthFailures   uint64 `json:"auth_failures"`
	BadKey         uint64 `json:"bad_key"`
	BadRoom        uint64 `json:"bad_room"`
	DroppedUnknown uint64 `json:"dropped_unknown"`
	DroppedQueue   uint64 `json:"dropped_queue"`
	RoutedPackets  uint64 `json:"routed_packets"`
	Clients        int64  `json:"clients"`
}

type adminOverviewView struct {
	Rooms         int            `json:"rooms"`
	Clients       int64          `json:"clients"`
	UptimeSeconds int64          `json:"uptime_seconds"`
	Stats         adminStatsView `json:"stats"`
}

type adminRoomView struct {
	Room    string   `json:"room"`
	Clients int      `json:"clients"`
	Devices []uint16 `json:"devices"`
}

func (s *relayServer) statsView() adminStatsView {
	return adminStatsView{
		AuthFailures:   s.stats.authFailures.Load(),
		BadKey:         s.stats.badKey.Load(),
		BadRoom:        s.stats.badRoom.Load(),
		DroppedUnknown: s.stats.droppedUnknown.Load(),
		DroppedQueue:   s.stats.droppedQueue.Load(),
		RoutedPackets:  s.stats.routedPackets.Load(),
		Clients:        s.stats.clients.Load(),
	}
}

func (s *relayServer) roomsView() []adminRoomView {
	s.mu.Lock()
	defer s.mu.Unlock()
	rooms := make([]adminRoomView, 0, len(s.rooms))
	for number, r := range s.rooms {
		devices := make([]uint16, 0, len(r.clients))
		for dev := range r.clients {
			devices = append(devices, dev)
		}
		sort.Slice(devices, func(i, j int) bool { return devices[i] < devices[j] })
		rooms = append(rooms, adminRoomView{
			Room:    number,
			Clients: len(r.clients),
			Devices: devices,
		})
	}
	sort.Slice(rooms, func(i, j int) bool { return rooms[i].Room < rooms[j].Room })
	return rooms
}

func (s *relayServer) adminAuth(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if *adminToken == "" {
			next.ServeHTTP(w, r)
			return
		}
		auth := r.Header.Get("Authorization")
		if auth == "Bearer "+*adminToken {
			next.ServeHTTP(w, r)
			return
		}
		w.Header().Set("WWW-Authenticate", `Bearer realm="relay-admin"`)
		http.Error(w, "unauthorized", http.StatusUnauthorized)
	})
}

func (s *relayServer) handleMetrics(w http.ResponseWriter, _ *http.Request) {
	s.mu.Lock()
	rooms := len(s.rooms)
	s.mu.Unlock()
	stats := s.statsView()
	w.Header().Set("Content-Type", "text/plain; version=0.0.4")
	fmt.Fprintf(w, "# HELP relay_server_rooms Current active rooms.\n")
	fmt.Fprintf(w, "# TYPE relay_server_rooms gauge\n")
	fmt.Fprintf(w, "relay_server_rooms %d\n", rooms)
	fmt.Fprintf(w, "# HELP relay_server_clients Current authenticated clients.\n")
	fmt.Fprintf(w, "# TYPE relay_server_clients gauge\n")
	fmt.Fprintf(w, "relay_server_clients %d\n", stats.Clients)
	fmt.Fprintf(w, "# HELP relay_server_routed_packets_total Routed WaNet packets.\n")
	fmt.Fprintf(w, "# TYPE relay_server_routed_packets_total counter\n")
	fmt.Fprintf(w, "relay_server_routed_packets_total %d\n", stats.RoutedPackets)
	fmt.Fprintf(w, "# HELP relay_server_auth_failures_total Authentication failures.\n")
	fmt.Fprintf(w, "# TYPE relay_server_auth_failures_total counter\n")
	fmt.Fprintf(w, "relay_server_auth_failures_total %d\n", stats.AuthFailures)
	fmt.Fprintf(w, "# HELP relay_server_dropped_unknown_total Packets with no route.\n")
	fmt.Fprintf(w, "# TYPE relay_server_dropped_unknown_total counter\n")
	fmt.Fprintf(w, "relay_server_dropped_unknown_total %d\n", stats.DroppedUnknown)
	fmt.Fprintf(w, "# HELP relay_server_dropped_queue_total DATA packets dropped on full outbound queue.\n")
	fmt.Fprintf(w, "# TYPE relay_server_dropped_queue_total counter\n")
	fmt.Fprintf(w, "relay_server_dropped_queue_total %d\n", stats.DroppedQueue)
}

func (s *relayServer) handleRotateServerKey(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	var body struct {
		ServerKey string `json:"server_key"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		http.Error(w, "bad json", http.StatusBadRequest)
		return
	}
	body.ServerKey = strings.TrimSpace(body.ServerKey)
	if body.ServerKey == "" || len(body.ServerKey) > 128 {
		http.Error(w, "server_key must be 1..128 bytes", http.StatusBadRequest)
		return
	}
	s.keyMu.Lock()
	s.key = body.ServerKey
	s.keyMu.Unlock()
	writeJSON(w, map[string]any{"rotated": true})
}

func (s *relayServer) serveAdmin() {
	mux := http.NewServeMux()
	mux.HandleFunc("/healthz", func(w http.ResponseWriter, _ *http.Request) {
		w.Header().Set("Content-Type", "text/plain")
		_, _ = w.Write([]byte("ok\n"))
	})
	mux.HandleFunc("/metrics", s.handleMetrics)
	mux.HandleFunc("/admin/overview", func(w http.ResponseWriter, _ *http.Request) {
		s.mu.Lock()
		rooms := len(s.rooms)
		s.mu.Unlock()
		view := adminOverviewView{
			Rooms:         rooms,
			Clients:       s.stats.clients.Load(),
			UptimeSeconds: int64(time.Since(s.started).Seconds()),
			Stats:         s.statsView(),
		}
		writeJSON(w, view)
	})
	mux.HandleFunc("/admin/rooms", func(w http.ResponseWriter, _ *http.Request) {
		writeJSON(w, s.roomsView())
	})
	mux.HandleFunc("/admin/server-key/rotate", s.handleRotateServerKey)
	mux.HandleFunc("/admin/rooms/", s.handleRoom)

	server := &http.Server{
		Addr:              *adminListen,
		Handler:           s.adminAuth(mux),
		ReadHeaderTimeout: 10 * time.Second,
	}
	log.Printf("relay admin listening on %s", *adminListen)
	if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		log.Printf("relay admin server: %v", err)
	}
}

func (s *relayServer) handleRoom(w http.ResponseWriter, r *http.Request) {
	rest := strings.TrimPrefix(r.URL.Path, "/admin/rooms/")
	if rest == "" {
		http.NotFound(w, r)
		return
	}
	parts := strings.Split(rest, "/")
	room := parts[0]
	if room == "" {
		http.NotFound(w, r)
		return
	}

	if len(parts) == 2 && parts[1] == "clients" {
		// /admin/rooms/{room}/clients
		view := s.roomView(room)
		if view == nil {
			http.NotFound(w, r)
			return
		}
		writeJSON(w, view)
		return
	}

	if len(parts) == 3 && parts[1] == "clients" {
		// /admin/rooms/{room}/clients/{device_id}
		var dev uint64
		if _, err := fmt.Sscanf(parts[2], "%x", &dev); err != nil || dev > 0xffff {
			http.Error(w, "bad device id", http.StatusBadRequest)
			return
		}
		if r.Method != http.MethodDelete {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		if !s.kickClient(room, uint16(dev)) {
			http.NotFound(w, r)
			return
		}
		writeJSON(w, map[string]any{"kicked": true})
		return
	}

	if r.Method == http.MethodDelete {
		if !s.deleteRoom(room) {
			http.NotFound(w, r)
			return
		}
		writeJSON(w, map[string]any{"deleted": true})
		return
	}
	view := s.roomView(room)
	if view == nil {
		http.NotFound(w, r)
		return
	}
	writeJSON(w, view)
}

func (s *relayServer) roomView(room string) *adminRoomView {
	s.mu.Lock()
	defer s.mu.Unlock()
	r := s.rooms[room]
	if r == nil {
		return nil
	}
	devices := make([]uint16, 0, len(r.clients))
	for dev := range r.clients {
		devices = append(devices, dev)
	}
	sort.Slice(devices, func(i, j int) bool { return devices[i] < devices[j] })
	return &adminRoomView{Room: room, Clients: len(r.clients), Devices: devices}
}

func (s *relayServer) deleteRoom(room string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	r := s.rooms[room]
	if r == nil {
		return false
	}
	for _, cl := range r.clients {
		s.closeClient(cl)
	}
	delete(s.rooms, room)
	return true
}

func (s *relayServer) kickClient(room string, dev uint16) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	r := s.rooms[room]
	if r == nil {
		return false
	}
	cl := r.clients[dev]
	if cl == nil {
		return false
	}
	s.closeClient(cl)
	delete(r.clients, dev)
	delete(r.hostAnn, dev)
	if len(r.clients) == 0 {
		delete(s.rooms, room)
	}
	return true
}

func writeJSON(w http.ResponseWriter, value any) {
	w.Header().Set("Content-Type", "application/json")
	if err := json.NewEncoder(w).Encode(value); err != nil {
		log.Printf("admin write json: %v", err)
	}
}

func (s *relayServer) writeAuthReply(conn net.Conn, status byte) {
	reply := make([]byte, authReplyLen)
	copy(reply[0:4], helloMagic)
	reply[4] = status
	reply[5] = 0
	_, _ = conn.Write(reply)
}

func (s *relayServer) writerLoop(cl *client) {
	for raw := range cl.out {
		if _, err := cl.conn.Write(raw); err != nil {
			_ = cl.conn.Close()
			return
		}
	}
}

func (s *relayServer) readLoop(cl *client) {
	buf := make([]byte, waPacketLen)
	for {
		if _, err := io.ReadFull(cl.conn, buf); err != nil {
			if err != io.EOF {
				log.Printf("client dev=%d room=%s read: %v", cl.dev, cl.room, err)
			}
			return
		}
		if !validPacket(buf) {
			continue
		}
		s.routePacket(cl, buf)
	}
}

func validPacket(raw []byte) bool {
	magic := binary.LittleEndian.Uint32(raw[0:4])
	version := raw[4]
	wordCount := raw[7]
	return magic == waMagic && version == waVersion && wordCount <= 24
}

type packetHeader struct {
	typ       byte
	dev       uint16
	target    uint16
	wordCount byte
}

func parseHeader(raw []byte) packetHeader {
	return packetHeader{
		typ:       raw[5],
		dev:       binary.LittleEndian.Uint16(raw[8:10]),
		target:    binary.LittleEndian.Uint16(raw[12:14]),
		wordCount: raw[7],
	}
}

func (s *relayServer) routePacket(cl *client, raw []byte) {
	h := parseHeader(raw)
	if *debug {
		log.Printf("relay route room=%q dev=%d target=%d type=%d words=%d",
			cl.room, h.dev, h.target, h.typ, h.wordCount)
	}
	s.mu.Lock()
	r := s.rooms[cl.room]
	if r == nil {
		s.mu.Unlock()
		return
	}
	target := r.clients[h.target]
	others := make([]*client, 0, len(r.clients))
	for dev, c := range r.clients {
		if dev != cl.dev {
			others = append(others, c)
		}
	}

	broadcastDiscovery := h.typ == waTypeAnnounce || h.typ == waTypeDiscover
	if (h.target == 0 || h.target == 0xffff) && broadcastDiscovery {
		if h.typ == waTypeAnnounce {
			cp := make([]byte, waPacketLen)
			copy(cp, raw)
			r.hostAnn[cl.dev] = cp
		}
		if h.typ == waTypeDiscover {
			for dev, ann := range r.hostAnn {
				if dev != cl.dev {
					s.enqueue(cl, ann)
				}
			}
		}
		for _, c := range others {
			s.enqueue(c, raw)
		}
		s.mu.Unlock()
		return
	}

	if target == nil {
		s.mu.Unlock()
		s.stats.droppedUnknown.Add(1)
		if *debug {
			log.Printf("relay route no target room=%q dev=%d target=%d type=%d",
				cl.room, h.dev, h.target, h.typ)
		}
		return
	}
	s.mu.Unlock()

	s.enqueue(target, raw)
	s.stats.routedPackets.Add(1)
}

func (s *relayServer) enqueue(c *client, raw []byte) {
	if c == nil {
		return
	}
	c.outMu.Lock()
	if c.closed {
		c.outMu.Unlock()
		return
	}
	cp := make([]byte, waPacketLen)
	copy(cp, raw)
	select {
	case c.out <- cp:
	default:
		h := parseHeader(raw)
		if h.typ == waTypeData {
			s.stats.droppedQueue.Add(1)
		} else {
			// Keep ordered command/control records under backpressure.
			select {
			case c.out <- cp:
			case <-c.done:
			}
		}
	}
	c.outMu.Unlock()
}
