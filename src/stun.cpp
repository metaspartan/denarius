/*
 * Get External IP address by STUN protocol
 *
 * Based on project Minimalistic STUN client "ministun"
 * https://code.google.com/p/ministun/
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 *
 * STUN is described in RFC3489 and it is based on the exchange
 * of UDP packets between a client and one or more servers to
 * determine the externally visible address (and port) of the client
 * once it has gone through the NAT boxes that connect it to the
 * outside.
 * The simplest request packet is just the header defined in
 * struct stun_header, and from the response we may just look at
 * one attribute, STUN_MAPPED_ADDRESS, that we find in the response.
 * By doing more transactions with different server addresses we
 * may determine more about the behaviour of the NAT boxes, of
 * course - the details are in the RFC.
 *
 * All STUN packets start with a simple header made of a type,
 * length (excluding the header) and a 16-byte random transaction id.
 * Following the header we may have zero or more attributes, each
 * structured as a type, length and a value (whose format depends
 * on the type, but often contains addresses).
 * Of course all fields are in network format.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#ifdef WIN32
#include <winsock2.h>
#include <stdint.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif
#include <unistd.h>
#include <time.h>
#include <errno.h>


#include "ministun.h"

/*---------------------------------------------------------------------*/

struct StunSrv {
    char     name[36];
    uint16_t port;
};

/*---------------------------------------------------------------------*/
// STUN server list from 2018-03-31
// Two are commented out, to keep prime list size
static const int StunSrvListQty = 277; // Must be PRIME!!!!!

static struct StunSrv StunSrvList[277] = {
        {"iphone-stun.strato-iphone.de",	3478},
        {"numb.viagenie.ca",	3478},
        {"sip1.lakedestiny.cordiaip.com",	3478},
        {"stun.12connect.com",	3478},
        {"stun.12voip.com",	3478},
        {"stun.1cbit.ru",	3478},
        {"stun.1und1.de",	3478},
        {"stun.2talk.co.nz",	3478},
        {"stun.2talk.com",	3478},
        {"stun.3clogic.com",	3478},
        {"stun.3cx.com",	3478},
        {"stun.a-mm.tv",	3478},
        {"stun.aa.net.uk",	3478},
        {"stun.aceweb.com",	3478},
        {"stun.acrobits.cz",	3478},
        {"stun.acronis.com",	3478},
        {"stun.actionvoip.com",	3478},
        {"stun.advfn.com",	3478},
        {"stun.aeta-audio.com",	3478},
        {"stun.aeta.com",	3478},
        {"stun.allflac.com",	3478},
        {"stun.anlx.net",	3478},
        {"stun.antisip.com",	3478},
        {"stun.artrage.com",	3478},
        {"stun.avigora.com",	3478},
        {"stun.avigora.fr",	3478},
        {"stun.b2b2c.ca",	3478},
        {"stun.bahnhof.net",	3478},
        {"stun.barracuda.com",	3478},
        {"stun.beam.pro",	3478},
        {"stun.bitburger.de",	3478},
        {"stun.bluesip.net",	3478},
        {"stun.bomgar.com",	3478},
        {"stun.botonakis.com",	3478},
        {"stun.budgetphone.nl",	3478},
        {"stun.budgetsip.com",	3478},
        {"stun.cablenet-as.net",	3478},
        {"stun.callromania.ro",	3478},
        {"stun.callwithus.com",	3478},
        {"stun.careerarc.com",	3478},
        {"stun.cheapvoip.com",	3478},
        {"stun.chinajot.com",	3478},
        {"stun.clan-gamers.de",	3478},
        {"stun.cloopen.com",	3478},
        {"stun.cognitoys.com",	3478},
        {"stun.commpeak.com",	3478},
        {"stun.comrex.com",	3478},
        {"stun.comtube.com",	3478},
        {"stun.comtube.ru",	3478},
        {"stun.connecteddata.com",	3478},
        {"stun.cope.es",	3478},
        {"stun.counterpath.com",	3478},
        {"stun.counterpath.net",	3478},
        {"stun.cozy.org",	3478},
        {"stun.crimeastar.net",	3478},
        {"stun.ctafauni.it",	3478},
        {"stun.dcalling.de",	3478},
        {"stun.demos.ru",	3478},
        {"stun.demos.su",	3478},
        {"stun.dls.net",	3478},
        {"stun.dokom.net",	3478},
        {"stun.dowlatow.ru",	3478},
        {"stun.duocom.es",	3478},
        {"stun.dus.net",	3478},
        {"stun.e-fon.ch",	3478},
        {"stun.easycall.pl",	3478},
        {"stun.easyvoip.com",	3478},
        {"stun.eibach.de",	3478},
        {"stun.ekiga.net",	3478},
        {"stun.ekir.de",	3478},
        {"stun.elitetele.com",	3478},
        {"stun.emu.ee",	3478},
        {"stun.engineeredarts.co.uk",	3478},
        {"stun.eoni.com",	3478},
        {"stun.epygi.com",	3478},
        {"stun.faceflow.com",	3478},
        {"stun.faktortel.com.au",	3478},
        {"stun.fbsbx.com",	3478},
        {"stun.fmo.de",	3478},
        {"stun.framasoft.org",	3478},
        {"stun.freecall.com",	3478},
        {"stun.freeswitch.org",	3478},
        {"stun.freevoipdeal.com",	3478},
        {"stun.funkfeuer.at",	3478},
        {"stun.gamenator.de",	3478},
        {"stun.genymotion.com",	3478},
        {"stun.gmx.de",	3478},
        {"stun.gmx.net",	3478},
        {"stun.gnunet.org",	3478},
        {"stun.gradwell.com",	3478},
        {"stun.halonet.pl",	3478},
        {"stun.hardt-ware.de",	3478},
        {"stun.healthtap.com",	3478},
        {"stun.highfidelity.io",	3478},
        {"stun.hoiio.com",	3478},
        {"stun.hosteurope.de",	3478},
        {"stun.huya.com",	3478},
        {"stun.ideasip.com",	3478},
        {"stun.inetvl.ru",	3478},
        {"stun.infra.net",	3478},
        {"stun.innovaphone.com",	3478},
        {"stun.instantteleseminar.com",	3478},
        {"stun.internetcalls.com",	3478},
        {"stun.intervoip.com",	3478},
        {"stun.invaluable.com",	3478},
        {"stun.ipcomms.net",	3478},
        {"stun.ipex.cz",	3478},
        {"stun.ipfire.org",	3478},
        {"stun.ippi.com",	3478},
        {"stun.ippi.fr",	3478},
        {"stun.it1.hr",	3478},
        {"stun.ivao.aero",	3478},
        {"stun.jabbim.cz",	3478},
        {"stun.jumblo.com",	3478},
        {"stun.justvoip.com",	3478},
        {"stun.kaospilot.dk",	3478},
        {"stun.kaseya.com",	3478},
        {"stun.kaznpu.kz",	3478},
        {"stun.kiwilink.co.nz",	3478},
        {"stun.l.google.com",	19302},
        {"stun.levigo.de",	3478},
        {"stun.lindab.com",	3478},
        {"stun.linphone.org",	3478},
        {"stun.linx.net",	3478},
        {"stun.liveo.fr",	3478},
        {"stun.lleida.net",	3478},
        {"stun.londonweb.net",	3478},
        {"stun.lovense.com",	3478},
        {"stun.lowratevoip.com",	3478},
        {"stun.lundimatin.fr",	3478},
        {"stun.maestroconference.com",	3478},
        {"stun.maglan.ru",	3478},
        {"stun.mangotele.com",	3478},
        {"stun.mda.gov.br",	3478},
        {"stun.mdaemon.com",	3478},
        {"stun.meritnation.com",	3478},
        {"stun.mgn.ru",	3478},
        {"stun.mit.de",	3478},
        {"stun.miwifi.com",	3478},
        {"stun.mixer.com",	3478},
        {"stun.modulus.gr",	3478},
        {"stun.mrmondialisation.org",	3478},
        {"stun.myfreecams.com",	3478},
        {"stun.myvoiptraffic.com",	3478},
        {"stun.mywatson.it",	3478},
        {"stun.nas.net",	3478},
        {"stun.naturalis.nl",	3478},
        {"stun.nautile.nc",	3478},
        {"stun.netappel.com",	3478},
        {"stun.nextcloud.com",	3478},
        {"stun.nfon.net",	3478},
        {"stun.ngine.de",	3478},
        {"stun.node4.co.uk",	3478},
        {"stun.nonoh.net",	3478},
        {"stun.nottingham.ac.uk",	3478},
        {"stun.nova.is",	3478},
        {"stun.okaybuy.com.cn",	3478},
        {"stun.onesuite.com",	3478},
        {"stun.onthenet.com.au",	3478},
        {"stun.ooma.com",	3478},
        {"stun.ozekiphone.com",	3478},
        {"stun.pados.hu",	3478},
        {"stun.personal-voip.de",	3478},
        {"stun.petcube.com",	3478},
        {"stun.pexip.com",	3478},
        {"stun.phone.com",	3478},
        {"stun.pidgin.im",	3478},
        {"stun.pjsip.org",	3478},
        {"stun.planete.net",	3478},
        {"stun.poivy.com",	3478},
        {"stun.powervoip.com",	3478},
        {"stun.ppdi.com",	3478},
        {"stun.qbictechnology.com",	3478},
        {"stun.rackco.com",	3478},
        {"stun.radiojar.com",	3478},
        {"stun.rainway.io",	3478},
        {"stun.redworks.nl",	3478},
        {"stun.ringostat.com",	3478},
        {"stun.rmf.pl",	3478},
        {"stun.rockenstein.de",	3478},
        {"stun.rolmail.net",	3478},
        {"stun.romancecompass.com",	3478},
        {"stun.rudtp.ru",	3478},
        {"stun.rynga.com",	3478},
        {"stun.sainf.ru",	3478},
        {"stun.schlund.de",	3478},
        {"stun.semiocast.com",	3478},
        {"stun.sigmavoip.com",	3478},
        {"stun.sintesis.com",	3478},
        {"stun.sip.us",	3478},
        {"stun.sipdiscount.com",	3478},
        {"stun.sipgate.net",	10000},
        {"stun.sipgate.net",	3478},
        {"stun.siplogin.de",	3478},
        {"stun.sipnet.net",	3478},
        {"stun.sipnet.ru",	3478},
        {"stun.siportal.it",	3478},
        {"stun.sippeer.dk",	3478},
        {"stun.siptraffic.com",	3478},
        {"stun.sketch.io",	3478},
        {"stun.sma.de",	3478},
        {"stun.smartvoip.com",	3478},
        {"stun.smsdiscount.com",	3478},
        {"stun.snafu.de",	3478},
        {"stun.solcon.nl",	3478},
        {"stun.solnet.ch",	3478},
        {"stun.sonetel.com",	3478},
        {"stun.sonetel.net",	3478},
        {"stun.sovtest.ru",	3478},
        {"stun.speedy.com.ar",	3478},
        {"stun.spoiltheprincess.com",	3478},
        {"stun.srce.hr",	3478},
        {"stun.ssl7.net",	3478},
        {"stun.stunprotocol.org",	3478},
        {"stun.swissquote.com",	3478},
        {"stun.t-online.de",	3478},
        {"stun.talks.by",	3478},
        {"stun.tel.lu",	3478},
        {"stun.telbo.com",	3478},
        {"stun.telefacil.com",	3478},
        {"stun.threema.ch",	3478},
        {"stun.tng.de",	3478},
        {"stun.twt.it",	3478},
        {"stun.ucw.cz",	3478},
        {"stun.uls.co.za",	3478},
        {"stun.unseen.is",	3478},
        {"stun.up.edu.ph",	3478},
        {"stun.usfamily.net",	3478},
        {"stun.vbuzzer.com",	3478},
        {"stun.veoh.com",	3478},
        {"stun.vipgroup.net",	3478},
        {"stun.viva.gr",	3478},
        {"stun.vivox.com",	3478},
        {"stun.vo.lu",	3478},
        {"stun.vodafone.ro",	3478},
        {"stun.voicetrading.com",	3478},
        {"stun.voip.aebc.com",	3478},
        {"stun.voip.blackberry.com",	3478},
        {"stun.voip.eutelia.it",	3478},
        {"stun.voiparound.com",	3478},
        {"stun.voipblast.com",	3478},
        {"stun.voipbuster.com",	3478},
        {"stun.voipbusterpro.com",	3478},
        {"stun.voipcheap.co.uk",	3478},
// {"stun.voipcheap.com",	3478},
        {"stun.voipdiscount.com",	3478},
        {"stun.voipfibre.com",	3478},
        {"stun.voipgain.com",	3478},
        {"stun.voipgate.com",	3478},
        {"stun.voipinfocenter.com",	3478},
        {"stun.voippro.com",	3478},
        {"stun.voipraider.com",	3478},
        {"stun.voipstunt.com",	3478},
        {"stun.voipwise.com",	3478},
        {"stun.voipzoom.com",	3478},
        {"stun.voxgratia.org",	3478},
        {"stun.voxox.com",	3478},
        {"stun.voztele.com",	3478},
        {"stun.wcoil.com",	3478},
        {"stun.webcalldirect.com",	3478},
// {"stun.websurf.ru",	3478},
        {"stun.whc.net",	3478},
        {"stun.whoi.edu",	3478},
        {"stun.wifirst.net",	3478},
        {"stun.wiseuc.com",	3478},
        {"stun.wwdl.net",	3478},
        {"stun.xn----8sbcoa5btidn9i.xn--p1ai",	3478},
        {"stun.xten.com",	3478},
        {"stun.xtratelecom.es",	3478},
        {"stun.yy.com",	3478},
        {"stun.zadarma.com",	3478},
        {"stun.zepter.ru",	3478},
        {"stun.zoiper.com",	3478},
        {"stun.zombiegrinder.com",	3478},
        {"stun1.faktortel.com.au",	3478},
        {"stun1.l.google.com",	19302},
        {"stun2.l.google.com",	19302},
        {"stun3.l.google.com",	19302},
        {"stun4.l.google.com",	19302},
};


/* wrapper to send an STUN message */
static int stun_send(int s, struct sockaddr_in *dst, struct stun_header *resp)
{
    return sendto(s, (const char *)resp, ntohs(resp->msglen) + sizeof(*resp), 0,
                  (struct sockaddr *)dst, sizeof(*dst));
}

/* helper function to generate a random request id */
static uint64_t randfiller;
static void stun_req_id(struct stun_header *req)
{
    const uint64_t *S_block = (const uint64_t *)StunSrvList;
    req->id.id[1] &= 0x55555555;
    req->id.id[2] |= 0x55555555;
    req->id.id[3] &= 0x55555555;
    char x = 20;
    do {
        uint32_t s_elm = S_block[(uint8_t)randfiller];
        randfiller = (randfiller << 5) | (randfiller >> (64 - 5));
        randfiller += s_elm ^ x;
        req->id.id[x & 3] += randfiller ^ (randfiller >> 19);
    } while(--x);
    req->id.id[0] = STUN_XORMAGIC; // Set magic for RFC5389
}


/* Extract the STUN_MAPPED_ADDRESS from the stun response.
 * This is used as a callback for stun_handle_response
 * when called from stun_request.
 */
static int stun_get_mapped(struct stun_attr *attr, void *arg)
{
    struct stun_addr *addr = (struct stun_addr *)(attr + 1);
    struct sockaddr_in *sa = (struct sockaddr_in *)arg;
    int rc = 0;
    if(ntohs(attr->len) != 8)
        return rc;

    uint32_t xor_mask;

    switch(ntohs(attr->attr)) {
        case STUN_MAPPED_ADDRESS:
            if(sa->sin_addr.s_addr == 0) {
                rc = 1;
                xor_mask = 0;
            }
            break;
        case STUN_XORMAPPEDADDRESS:
            rc = 2;
            xor_mask = STUN_XORMAGIC;
            break;
        case STUN_XORMAPPEDADDRESS2:
            rc = 4;
            xor_mask = STUN_XORMAGIC;
            break;
        default: break;
    }

    if(rc) {
        sa->sin_port        = addr->port ^ xor_mask;
        sa->sin_addr.s_addr = addr->addr ^ xor_mask;
    }

    return rc;
}


/* handle an incoming STUN message.
 *
 * Do some basic sanity checks on packet size and content,
 * try to extract a bit of information, and possibly reply.
 * At the moment this only processes BIND requests, and returns
 * the externally visible address of the request.
 * If a callback is specified, invoke it with the attribute.
 */
static int stun_handle_packet(int s, struct sockaddr_in *src,
                              unsigned char *data, size_t len, void *arg)
{
    struct stun_header *hdr = (struct stun_header *)data;
    struct stun_attr *attr;
    int ret = 0;
    size_t x;

    /* On entry, 'len' is the length of the udp payload. After the
     * initial checks it becomes the size of unprocessed options,
     * while 'data' is advanced accordingly.
     */
    if (len < sizeof(struct stun_header))
        return -20;

    len -= sizeof(struct stun_header);
    data += sizeof(struct stun_header);
    x = ntohs(hdr->msglen);	/* len as advertised in the message */
    if(x < len)
        len = x;

    while (len) {
        if (len < sizeof(struct stun_attr)) {
            ret = -21;
            break;
        }
        attr = (struct stun_attr *)data;
        /* compute total attribute length */
        x = ntohs(attr->len) + sizeof(struct stun_attr);
        if (x > len) {
            ret = -22;
            break;
        }
        ret |= stun_get_mapped(attr, arg);
        /* Clear attribute id: in case previous entry was a string,
         * this will act as the terminator for the string.
         */
        attr->attr = 0;
        data += x;
        len -= x;
    } // while
    /* Null terminate any string.
     * XXX NOTE, we write past the size of the buffer passed by the
     * caller, so this is potentially dangerous. The only thing that
     * saves us is that usually we read the incoming message in a
     * much larger buffer
     */
    *data = '\0';

    /* Now prepare to generate a reply, which at the moment is done
     * only for properly formed (len == 0) STUN_BINDREQ messages.
     */

    return ret;
}
/*---------------------------------------------------------------------*/

static int StunRequest2(int sock, struct sockaddr_in *server, struct sockaddr_in *mapped) {

    struct stun_header *req;
    unsigned char reqdata[1024];

    req = (struct stun_header *)reqdata;
    stun_req_id(req);
    int reqlen = 0;
    req->msgtype = 0;
    req->msglen = 0;
    req->msglen = htons(reqlen);
    req->msgtype = htons(STUN_BINDREQ);

    unsigned char reply_buf[1024];
    fd_set rfds;
    struct timeval to = { STUN_TIMEOUT, 0 };
    struct sockaddr_in src;
#ifdef WIN32
    int srclen;
#else
    socklen_t srclen;
#endif

    int res = stun_send(sock, server, req);
    if(res < 0)
        return -10;
    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);
    res = select(sock + 1, &rfds, NULL, NULL, &to);
    if (res <= 0) 	/* timeout or error */
        return -11;
    memset(&src, 0, sizeof(src));
    srclen = sizeof(src);
    /* XXX pass -1 in the size, because stun_handle_packet might
     * write past the end of the buffer.
     */
    res = recvfrom(sock, (char *)reply_buf, sizeof(reply_buf) - 1,
                   0, (struct sockaddr *)&src, &srclen);
    if (res <= 0)
        return -12;
    memset(mapped, 0, sizeof(struct sockaddr_in));
    return stun_handle_packet(sock, &src, reply_buf, res, mapped);
} // StunRequest2

/*---------------------------------------------------------------------*/
static int StunRequest(const char *host, uint16_t port, struct sockaddr_in *mapped) {
    struct hostent *hostinfo = gethostbyname(host);
    if(hostinfo == NULL)
        return -1;

    struct sockaddr_in server, client;
    memset(&server, 0, sizeof(server));
    memset(&client, 0, sizeof(client));
    server.sin_family = client.sin_family = AF_INET;

    server.sin_addr = *(struct in_addr*) hostinfo->h_addr;
    server.sin_port = htons(port);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock < 0)
        return -2;

    client.sin_addr.s_addr = htonl(INADDR_ANY);

    int rc = -3;
    if(bind(sock, (struct sockaddr*)&client, sizeof(client)) >= 0)
        rc = StunRequest2(sock, &server, mapped);

    close(sock);
    return rc;
} // StunRequest

/*---------------------------------------------------------------------*/
// Input: random value to generate the pair (pos, step) for pseudorandom
// traversal over the server list
// Output:
//  - mapped: populate struct struct mapped (ipV4 only)
//  - srv: set pointer to server name, which return successful answer
// Retval:
// bits 0-7 = STUN tokens set, 8-32 = attempt number
int GetExternalIPbySTUN(uint64_t rnd, struct sockaddr_in *mapped, const char **srv) {
    randfiller    = rnd;
    uint16_t pos  = rnd;
    uint16_t step;
    do {
        rnd = (rnd >> 8) | 0xff00000000000000LL;
        step = rnd % StunSrvListQty;
    } while(step == 0);

    int attempt; // runs in 8 birs offset, for keep flags in low byte
    for(attempt = 256; attempt < StunSrvListQty * 2 * 256; attempt += 256) {
        pos = (pos + step) % StunSrvListQty;
        int rc = StunRequest(*srv = StunSrvList[pos].name, StunSrvList[pos].port, mapped);
        if(rc > 0)
            return attempt | rc;
    }
    return -1;
}

/*---------------------------------------------------------------------*/