// Host audio out / audio in / MIDI in / MIDI out for Linux (ALSA PCM + ALSA sequencer).
// Same interface as host_io_win.h; the device id of the audio classes is the ALSA PCM name
// (as a std::wstring, to keep the Windows header unchanged).
#ifndef _WIN32
#include "host_io_win.h"

#include <alsa/asoundlib.h>
#include <pthread.h>
#include <chrono>
#include <cstring>

namespace MS2000 {

namespace {

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }
std::string narrow(const std::wstring& s) { return std::string(s.begin(), s.end()); }

// PCM names worth offering: "default" first, then the named endpoints ALSA reports (PipeWire/Pulse show up as
// "pipewire"/"pulse", hardware as "sysdefault:CARD=..." / "hw:..."). Direction: "Output" / "Input" / null (both).
std::vector<HostDevice> listPcm(bool capture)
{
    std::vector<HostDevice> out;
    out.push_back({ L"default", "default" });
    void** hints = nullptr;
    if (snd_device_name_hint(-1, "pcm", &hints) < 0) return out;
    for (void** h = hints; *h; ++h) {
        char* name = snd_device_name_get_hint(*h, "NAME");
        char* desc = snd_device_name_get_hint(*h, "DESC");
        char* ioid = snd_device_name_get_hint(*h, "IOID");
        const bool dirOk = !ioid || !std::strcmp(ioid, capture ? "Input" : "Output");
        if (name && dirOk && std::strcmp(name, "null") != 0 && std::strcmp(name, "default") != 0
            && (!std::strncmp(name, "sysdefault", 10) || !std::strncmp(name, "pipewire", 8) || !std::strncmp(name, "pulse", 5)
                || !std::strncmp(name, "plughw", 6) || !std::strncmp(name, "front", 5))) {
            std::string label = name;
            if (desc) { std::string d = desc; for (char& c : d) if (c == '\n') c = ' '; label += "  (" + d + ")"; }
            out.push_back({ widen(name), label });
        }
        if (name) free(name);
        if (desc) free(desc);
        if (ioid) free(ioid);
    }
    snd_device_name_free_hint(hints);
    return out;
}

} // namespace

// ----------------------------------------------------------------------------- audio out

std::vector<HostDevice> HostAudioOut::list() { return listPcm(false); }

bool HostAudioOut::open(const std::wstring& deviceId, Pull pull, uint32_t bufferMs)
{
    close();
    m_pull = std::move(pull);
    m_bufferMs = bufferMs;
    m_stop = false; m_dead = false; m_underrun = 0;
    { std::lock_guard<std::mutex> l(m_errMx); m_err.clear(); }

    // open here, so a failure is reported to the caller; the thread owns the handle afterwards
    snd_pcm_t* pcm = nullptr;
    const std::string name = narrow(deviceId);
    int rc = snd_pcm_open(&pcm, name.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (rc < 0) { fail(std::string("snd_pcm_open ") + name + ": " + snd_strerror(rc)); return false; }
    rc = snd_pcm_set_params(pcm, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, 48000, 1,
                            bufferMs * 1000u);
    if (rc < 0) { fail(std::string("snd_pcm_set_params: ") + snd_strerror(rc)); snd_pcm_close(pcm); return false; }
    m_devRate = 48000;
    m_handle = pcm;
    m_thread = std::thread([this] { run(L""); });
    return true;
}

void HostAudioOut::run(std::wstring)
{
    snd_pcm_t* pcm = static_cast<snd_pcm_t*>(m_handle);
    snd_pcm_uframes_t bufFrames = 0, periodFrames = 0;
    snd_pcm_get_params(pcm, &bufFrames, &periodFrames);
    if (periodFrames < 64) periodFrames = 64;
    std::vector<float> buf(size_t(periodFrames) * 2);

    // fill the device buffer once with silence so the first pull has a full buffer's lead
    {
        std::vector<float> z(size_t(bufFrames) * 2, 0.0f);
        snd_pcm_writei(pcm, z.data(), bufFrames);
    }
    int failures = 0;
    while (!m_stop.load()) {
        const uint32_t real = m_pull(buf.data(), uint32_t(periodFrames));
        if (real < periodFrames) m_underrun += periodFrames - real;
        const float* p = buf.data();
        snd_pcm_uframes_t left = periodFrames;
        while (left > 0 && !m_stop.load()) {
            const snd_pcm_sframes_t n = snd_pcm_writei(pcm, p, left);
            if (n < 0) {
                const int rc = snd_pcm_recover(pcm, int(n), 1);
                if (rc < 0) {
                    if (++failures > 20) { fail(std::string("ALSA write: ") + snd_strerror(rc)); m_dead = true; goto out; }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                continue;
            }
            failures = 0;
            p += size_t(n) * 2; left -= snd_pcm_uframes_t(n);
        }
    }
out:
    if (!m_stop.load()) m_dead = true;
    snd_pcm_drop(pcm);
}

void HostAudioOut::close()
{
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
    if (m_handle) { snd_pcm_close(static_cast<snd_pcm_t*>(m_handle)); m_handle = nullptr; }
    m_dead = false;
}

// ----------------------------------------------------------------------------- audio in

std::vector<HostDevice> HostAudioIn::list() { return listPcm(true); }

bool HostAudioIn::open(const std::wstring& deviceId, Sink sink)
{
    close();
    m_sink = std::move(sink);
    m_stop = false; m_dead = false; m_frames = 0;
    { std::lock_guard<std::mutex> l(m_errMx); m_err.clear(); }
    snd_pcm_t* pcm = nullptr;
    const std::string name = narrow(deviceId);
    int rc = snd_pcm_open(&pcm, name.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0) { m_err = std::string("snd_pcm_open ") + name + ": " + snd_strerror(rc); return false; }
    rc = snd_pcm_set_params(pcm, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, 48000, 1, 20000);
    if (rc < 0) { m_err = std::string("snd_pcm_set_params: ") + snd_strerror(rc); snd_pcm_close(pcm); return false; }
    m_handle = pcm;
    m_thread = std::thread([this] { run(L""); });
    return true;
}

void HostAudioIn::run(std::wstring)
{
    snd_pcm_t* pcm = static_cast<snd_pcm_t*>(m_handle);
    std::vector<float> buf(480 * 2);
    int failures = 0;
    snd_pcm_prepare(pcm);
    snd_pcm_start(pcm);
    while (!m_stop.load()) {
        const snd_pcm_sframes_t n = snd_pcm_readi(pcm, buf.data(), 480);
        if (n < 0) {
            const int rc = snd_pcm_recover(pcm, int(n), 1);
            if (rc < 0 && ++failures > 20) { m_err = std::string("ALSA read: ") + snd_strerror(rc); m_dead = true; break; }
            if (rc < 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            else snd_pcm_start(pcm);
            continue;
        }
        failures = 0;
        if (n > 0) { m_sink(buf.data(), uint32_t(n)); m_frames += uint64_t(n); }
    }
    snd_pcm_drop(pcm);
}

void HostAudioIn::close()
{
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
    if (m_handle) { snd_pcm_close(static_cast<snd_pcm_t*>(m_handle)); m_handle = nullptr; }
    m_dead = false;
}

// ----------------------------------------------------------------------------- MIDI (ALSA sequencer)

namespace {

struct SeqPort { int client, port; std::string name; };

// Index 0 is always the virtual port the application creates itself (connect it with aconnect / qjackctl /
// a patchbay); the others are the ports of the other clients that can be read (MIDI IN) or written (MIDI OUT).
std::vector<SeqPort> listSeq(bool wantReadable)
{
    std::vector<SeqPort> v;
    v.push_back({ -1, -1, wantReadable ? "MS2000R IN (virtual port)" : "MS2000R OUT (virtual port)" });
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) return v;
    snd_seq_client_info_t* ci; snd_seq_port_info_t* pi;
    snd_seq_client_info_alloca(&ci); snd_seq_port_info_alloca(&pi);
    snd_seq_client_info_set_client(ci, -1);
    const unsigned need = wantReadable ? (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)
                                       : (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE);
    while (snd_seq_query_next_client(seq, ci) >= 0) {
        const int client = snd_seq_client_info_get_client(ci);
        if (client == SND_SEQ_CLIENT_SYSTEM || client == snd_seq_client_id(seq)) continue;
        snd_seq_port_info_set_client(pi, client);
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(seq, pi) >= 0) {
            const unsigned caps = snd_seq_port_info_get_capability(pi);
            if ((caps & need) != need || (caps & SND_SEQ_PORT_CAP_NO_EXPORT)) continue;
            char label[160];
            std::snprintf(label, sizeof label, "%s: %s [%d:%d]", snd_seq_client_info_get_name(ci),
                          snd_seq_port_info_get_name(pi), client, snd_seq_port_info_get_port(pi));
            v.push_back({ client, snd_seq_port_info_get_port(pi), label });
        }
    }
    snd_seq_close(seq);
    return v;
}

struct SeqState {
    snd_seq_t* seq = nullptr;
    int port = -1;
    snd_midi_event_t* coder = nullptr;
    std::thread th;
    std::atomic<bool> stop{false};
};

} // namespace

// The headers keep a `void* m_handle` for the Windows handle; here it points at a SeqState.

std::vector<std::string> HostMidiIn::list()
{
    std::vector<std::string> n;
    for (auto& p : listSeq(true)) n.push_back(p.name);
    return n;
}

bool HostMidiIn::open(unsigned index, Sink sink)
{
    close();
    const auto ports = listSeq(true);
    if (index >= ports.size()) return false;
    auto* s = new SeqState;
    if (snd_seq_open(&s->seq, "default", SND_SEQ_OPEN_INPUT, 0) < 0) { delete s; return false; }
    snd_seq_set_client_name(s->seq, "MS2000R");
    s->port = snd_seq_create_simple_port(s->seq, "MIDI IN", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                         SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (s->port < 0) { snd_seq_close(s->seq); delete s; return false; }
    if (ports[index].client >= 0)
        snd_seq_connect_from(s->seq, s->port, ports[index].client, ports[index].port);
    snd_midi_event_new(65536, &s->coder);
    snd_midi_event_no_status(s->coder, 1);   // every message complete, no running status
    m_sink = std::move(sink);
    m_closing = false;
    m_handle = s;
    s->th = std::thread([this, s] {
        const int nfds = snd_seq_poll_descriptors_count(s->seq, POLLIN);
        std::vector<pollfd> fds(static_cast<size_t>(nfds));
        snd_seq_poll_descriptors(s->seq, fds.data(), unsigned(nfds), POLLIN);
        std::vector<uint8_t> buf(65536);
        while (!s->stop.load()) {
            if (poll(fds.data(), nfds_t(nfds), 50) <= 0) continue;
            snd_seq_event_t* ev = nullptr;
            while (snd_seq_event_input(s->seq, &ev) >= 0 && ev) {
                const long n = snd_midi_event_decode(s->coder, buf.data(), long(buf.size()), ev);
                if (n > 0) { m_sink(buf.data(), size_t(n)); m_bytes += uint64_t(n); }
                if (snd_seq_event_input_pending(s->seq, 0) <= 0) break;
            }
        }
    });
    return true;
}

void HostMidiIn::close()
{
    m_closing = true;
    auto* s = static_cast<SeqState*>(m_handle);
    if (!s) return;
    s->stop = true;
    if (s->th.joinable()) s->th.join();
    if (s->coder) snd_midi_event_free(s->coder);
    snd_seq_close(s->seq);
    delete s;
    m_handle = nullptr;
}

// the winmm callbacks do not exist here
void HostMidiIn::onShort(uint32_t) {}
void HostMidiIn::onLong(void*) {}

std::vector<std::string> HostMidiOut::list()
{
    std::vector<std::string> n;
    for (auto& p : listSeq(false)) n.push_back(p.name);
    return n;
}

bool HostMidiOut::open(unsigned index)
{
    close();
    const auto ports = listSeq(false);
    if (index >= ports.size()) return false;
    auto* s = new SeqState;
    if (snd_seq_open(&s->seq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) { delete s; return false; }
    snd_seq_set_client_name(s->seq, "MS2000R");
    s->port = snd_seq_create_simple_port(s->seq, "MIDI OUT", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
                                         SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (s->port < 0) { snd_seq_close(s->seq); delete s; return false; }
    if (ports[index].client >= 0)
        snd_seq_connect_to(s->seq, s->port, ports[index].client, ports[index].port);
    snd_midi_event_new(65536, &s->coder);
    snd_midi_event_no_status(s->coder, 1);
    std::lock_guard<std::mutex> l(m_mx);
    m_status = 0; m_have = m_need = 0; m_inSysex = false; m_sysex.clear();
    m_handle = s;
    return true;
}

void HostMidiOut::close()
{
    std::lock_guard<std::mutex> l(m_mx);
    auto* s = static_cast<SeqState*>(m_handle);
    if (!s) return;
    m_handle = nullptr;
    if (s->coder) snd_midi_event_free(s->coder);
    snd_seq_close(s->seq);
    delete s;
}

// byte(): the sequencer's own coder regroups the SCI byte stream into events (running status, sysex, realtime).
void HostMidiOut::byte(uint8_t b)
{
    std::lock_guard<std::mutex> l(m_mx);
    auto* s = static_cast<SeqState*>(m_handle);
    if (!s) return;
    ++m_bytes;
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    if (snd_midi_event_encode_byte(s->coder, b, &ev) == 1) {
        snd_seq_ev_set_source(&ev, static_cast<unsigned char>(s->port));
        snd_seq_ev_set_subs(&ev);
        snd_seq_ev_set_direct(&ev);
        snd_seq_event_output_direct(s->seq, &ev);
    }
}

void HostMidiOut::sendShort(const uint8_t*, size_t) {}
void HostMidiOut::sendSysex() {}
void HostMidiOut::reapDone() {}

} // namespace MS2000
#endif
