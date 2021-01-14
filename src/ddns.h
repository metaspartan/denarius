#ifndef DDNS_H
#define DDNS_H

#include <boost/thread.hpp>

#define DDNS_DAPSIZE     (8 * 1024)
#define DDNS_DAPTRESHOLD 300 // 20K/min limit answer

struct DNSHeader {
  static const uint32_t QR_MASK = 0x8000;
  static const uint32_t OPCODE_MASK = 0x7800; // shr 11
  static const uint32_t AA_MASK = 0x0400;
  static const uint32_t TC_MASK = 0x0200;
  static const uint32_t RD_MASK = 0x0100;
  static const uint32_t RA_MASK = 0x8000;
  static const uint32_t RCODE_MASK = 0x000F;

  uint16_t msgID;
  uint16_t Bits;
  uint16_t QDCount;
  uint16_t ANCount;
  uint16_t NSCount;
  uint16_t ARCount;

  inline void Transcode() {
    for(uint16_t *p = &msgID; p <= &ARCount; p++)
      *p = ntohs(*p);
  }
} __attribute__((packed)); // struct DNSHeader


struct DNSAP {		// DNS Amplifier Protector ExpDecay structure
  uint16_t timestamp;	// Time in 64s ticks
  uint16_t ed_size;	// ExpDecay output size in 64-byte units
} __attribute__((packed));

class DDns {
  public:
     DDns(const char *bind_ip, uint16_t port_no,
	    const char *gw_suffix, const char *allowed_suff, const char *local_fname, uint8_t verbose);
    ~DDns();

    void Run();

  private:
    static void StatRun(void *p);
    void HandlePacket();
    uint16_t HandleQuery();
    int  Search(uint8_t *key);
    int  LocalSearch(const uint8_t *key, uint8_t pos, uint8_t step);
    int  Tokenize(const char *key, const char *sep2, char **tokens, char *buf);
    void Answer_ALL(uint16_t qtype, char *buf);
    void Fill_RD_IP(char *ipddrtxt, int af);
    void Fill_RD_DName(char *txt, uint8_t mxsz, int8_t txtcor);
    int  TryMakeref(uint16_t label_ref);

    // Returns x = hash index to update size; x==NULL = disable;
    DNSAP  *CheckDAP(uint32_t ip_addr);

    inline void Out2(uint16_t x) { x = htons(x); memcpy(m_snd, &x, 2); m_snd += 2; }
    inline void Out4(uint32_t x) { x = htonl(x); memcpy(m_snd, &x, 4); m_snd += 4; }

    DNSHeader *m_hdr;
    DNSAP    *m_dap_ht;	// Hashtable for DAP; index is hash(IP)
    char     *m_value;
    const char *m_gw_suffix;
    uint8_t  *m_buf, *m_bufend, *m_snd, *m_rcv, *m_rcvend;
    SOCKET    m_sockfd;
    int       m_rcvlen;
    uint32_t  m_daprand;	// DAP random value for universal hashing
    uint32_t  m_ttl;
    uint16_t  m_label_ref;
    uint16_t  m_gw_suf_len;
    uint8_t   m_gw_suf_dots;
    uint8_t   m_verbose;
    uint8_t   m_allowed_qty;
    uint8_t   m_status;
    char     *m_allowed_base;
    char     *m_local_base;
    int16_t   m_ht_offset[0x100]; // Hashtable for allowed TLD-suffixes(>0) and local names(<0)
    struct sockaddr_in m_clientAddress;
    struct sockaddr_in m_address;
    socklen_t m_addrLen;

    boost::thread m_thread;
}; // class DDns

#endif // DDNS_H