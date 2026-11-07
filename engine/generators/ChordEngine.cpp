#include "ChordEngine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <random>

namespace jaudios::generators
{

using theory::Chord;
using theory::Key;
using theory::Mode;
using theory::Quality;

const char* styleName (Style s) noexcept
{
    static const char* names[numStyles] = { "pop", "house", "techno", "trance", "lofi", "rnb", "cinematic" };
    return names[(int) s];
}

std::optional<Style> styleFromName (const std::string& name)
{
    std::string n;
    for (char c : name)
        if (std::isalnum ((unsigned char) c))
            n += (char) std::tolower ((unsigned char) c);
    if (n == "rb") n = "rnb";
    for (int s = 0; s < numStyles; ++s)
        if (n == styleName ((Style) s))
            return (Style) s;
    return std::nullopt;
}

namespace
{
Template t (const std::string& seq, double w)
{
    Template out { seq, {}, w };
    size_t start = 0;
    while (start <= seq.size())
    {
        const auto end = seq.find ('-', start);
        out.romans.push_back (seq.substr (start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return out;
}
} // namespace

const std::vector<Template>& templates (Style style, Mode mode)
{
    // Weighted degree sequences per style (skill: pop, house, techno, trance, lo-fi, R&B, cinematic),
    // with modal-mixture chords (bVI, bVII, iv in major; V / IV in minor). Our own lists, written for
    // this engine.
    static const std::map<std::pair<int, int>, std::vector<Template>> table {
        { { (int) Style::pop, 0 }, { t ("I-V-vi-IV", 1.0), t ("vi-IV-I-V", 0.9), t ("I-vi-IV-V", 0.8), t ("I-IV-vi-V", 0.7),
                                     t ("IV-I-V-vi", 0.6), t ("I-V-IV-V", 0.5), t ("ii-V-I-I", 0.4), t ("I-bVII-IV-I", 0.35) } },
        { { (int) Style::pop, 1 }, { t ("i-VI-III-VII", 1.0), t ("i-VII-VI-VII", 0.8), t ("VI-VII-i-i", 0.7), t ("i-iv-VI-V", 0.6),
                                     t ("i-VI-VII-i", 0.5), t ("i-iv-v-i", 0.4) } },
        { { (int) Style::house, 0 }, { t ("vi-IV-I-V", 1.0), t ("I-V-vi-IV", 0.8), t ("ii-V-I-vi", 0.6), t ("I-iii-IV-V", 0.5),
                                       t ("IV-V-iii-vi", 0.5), t ("I-bVII-IV-IV", 0.3) } },
        { { (int) Style::house, 1 }, { t ("i-VI-III-VII", 1.0), t ("i-VII-VI-VII", 0.8), t ("i-iv-VI-VII", 0.7), t ("i-VI-iv-VII", 0.6),
                                       t ("i-i-iv-i", 0.5) } },
        { { (int) Style::techno, 0 }, { t ("I-I-IV-I", 1.0), t ("I-bVII-I-bVII", 0.7), t ("I-IV-I-V", 0.5), t ("I-I-I-bVII", 0.4) } },
        { { (int) Style::techno, 1 }, { t ("i-i-iv-i", 1.0), t ("i-VI-i-VII", 0.8), t ("i-i-VI-VII", 0.6), t ("i-iv-i-VII", 0.5),
                                        t ("i-i-i-VI", 0.4) } },
        { { (int) Style::trance, 0 }, { t ("vi-IV-I-V", 1.0), t ("IV-V-vi-vi", 0.8), t ("I-V-vi-IV", 0.7), t ("vi-V-IV-V", 0.5) } },
        { { (int) Style::trance, 1 }, { t ("i-VI-III-VII", 1.0), t ("i-VI-VII-V", 0.8), t ("VI-VII-i-i", 0.7), t ("i-III-VII-VI", 0.6),
                                        t ("i-iv-VI-VII", 0.5) } },
        { { (int) Style::lofi, 0 }, { t ("Imaj7-vi7-ii7-V7", 1.0), t ("ii7-V7-Imaj7-Imaj7", 0.9), t ("IVmaj7-iii7-ii7-Imaj7", 0.7),
                                      t ("Imaj7-IVmaj7-iii7-vi7", 0.6), t ("IVmaj7-iv-Imaj7-Imaj7", 0.4) } },
        { { (int) Style::lofi, 1 }, { t ("i7-iv7-VII7-IIImaj7", 1.0), t ("i7-VImaj7-iv7-V7", 0.8), t ("i7-iv7-i7-v7", 0.6),
                                      t ("VImaj7-v7-i7-i7", 0.5) } },
        { { (int) Style::rnb, 0 }, { t ("Imaj7-vi7-ii7-V7", 1.0), t ("IVmaj7-iii7-vi7-ii7", 0.8), t ("ii7-V7-iii7-vi7", 0.7),
                                     t ("Imaj7-IVmaj7-iv-Imaj7", 0.5) } },
        { { (int) Style::rnb, 1 }, { t ("i7-iv7-VImaj7-V7", 1.0), t ("i7-VImaj7-IIImaj7-VII7", 0.8), t ("iv7-v7-i7-i7", 0.6) } },
        { { (int) Style::cinematic, 0 }, { t ("I-bVI-bVII-I", 1.0), t ("I-iii-IV-iv", 0.9), t ("vi-IV-I-V", 0.8), t ("I-V-vi-iii", 0.6),
                                           t ("IV-bVI-I-I", 0.5) } },
        { { (int) Style::cinematic, 1 }, { t ("i-VI-III-VII", 1.0), t ("i-VI-iv-V", 0.9), t ("i-VII-VI-V", 0.8), t ("i-iv-VI-VII", 0.6),
                                           t ("i-III-iv-VI", 0.5) } },
    };
    return table.at ({ (int) style, mode == Mode::major ? 0 : 1 });
}

//==============================================================================
std::vector<Slot> makeSlots (const std::vector<BarEvidence>& bars, HarmonicRhythm rhythm)
{
    std::vector<Slot> slots;
    for (size_t b = 0; b < bars.size(); ++b)
    {
        const auto& bar = bars[b];
        switch (rhythm)
        {
            case HarmonicRhythm::onePerBar:
                slots.push_back ({ bar.startSec, bar.endSec, (int) b, 1 });
                break;
            case HarmonicRhythm::twoPerBar:
            {
                const double mid = 0.5 * (bar.startSec + bar.endSec);
                slots.push_back ({ bar.startSec, mid, (int) b, 1 });
                slots.push_back ({ mid, bar.endSec, (int) b, 1 });
                break;
            }
            case HarmonicRhythm::onePerTwoBars:
                if (b % 2 == 0)
                    slots.push_back ({ bar.startSec, bar.endSec, (int) b, 1 });
                else
                {
                    slots.back().endSec = bar.endSec;
                    slots.back().numBars = 2;
                }
                break;
        }
    }
    return slots;
}

std::vector<Chord> vocabulary (Key key, Style, bool sevenths)
{
    const bool major = key.mode == Mode::major;
    std::vector<std::string> romans = major
        ? std::vector<std::string> { "I", "ii", "iii", "IV", "V", "vi", "vii°", "bVI", "bVII", "iv", "bIII", "Isus2", "Isus4", "Vsus4" }
        : std::vector<std::string> { "i", "ii°", "III", "iv", "v", "VI", "VII", "V", "IV", "bII", "Isus2" };
    if (sevenths)
    {
        const auto more = major ? std::vector<std::string> { "Imaj7", "ii7", "iii7", "IVmaj7", "V7", "vi7", "Iadd9" }
                                : std::vector<std::string> { "i7", "iv7", "v7", "VImaj7", "IIImaj7", "VII7", "V7" };
        romans.insert (romans.end(), more.begin(), more.end());
    }
    std::vector<Chord> out;
    for (const auto& r : romans)
        if (const auto d = theory::parseRoman (r, key.mode))
            out.push_back (theory::chordOf (key, *d));
    return out;
}

namespace
{
inline int pcMod (int x) noexcept { return ((x % 12) + 12) % 12; }

Quality reduce (Quality q, bool sevenths)
{
    if (sevenths)
        return q;
    switch (q)
    {
        case Quality::dom7: case Quality::maj7: case Quality::add9: return Quality::maj;
        case Quality::min7: return Quality::min;
        default: return q;
    }
}

bool sameChord (const Chord& a, const Chord& b) noexcept { return a.root == b.root && a.quality == b.quality; }

/** Per-slot evidence, precomputed once per suggest(). */
struct SlotEvidence
{
    std::array<double, 12> melody {}, bass {};
    double melodyTotal = 0.0, bassTotal = 0.0;
    std::optional<std::array<double, 12>> chroma;
};

std::vector<SlotEvidence> gather (const HarmonicSource& src, const std::vector<Slot>& slots)
{
    std::vector<SlotEvidence> ev (slots.size());
    auto add = [] (const std::vector<SourceNote>& notes, const Slot& s, std::array<double, 12>& w, double& total) {
        for (const auto& n : notes)
        {
            const double overlap = std::min (n.endSec, s.endSec) - std::max (n.startSec, s.startSec);
            if (overlap > 0.0)
            {
                w[(size_t) pcMod (n.pitch)] += overlap * n.weight;
                total += overlap * n.weight;
            }
        }
    };
    for (size_t i = 0; i < slots.size(); ++i)
    {
        add (src.melody, slots[i], ev[i].melody, ev[i].melodyTotal);
        add (src.bass, slots[i], ev[i].bass, ev[i].bassTotal);
        std::array<double, 12> c {};
        bool any = false;
        for (int b = slots[i].firstBar; b < slots[i].firstBar + slots[i].numBars && b < (int) src.bars.size(); ++b)
            if (src.bars[(size_t) b].chroma)
            {
                any = true;
                for (size_t k = 0; k < 12; ++k)
                    c[k] += (*src.bars[(size_t) b].chroma)[k];
            }
        if (any)
            ev[i].chroma = c;
    }
    return ev;
}

struct Context
{
    const HarmonicSource& src;
    const EngineSettings& settings;
    std::vector<Slot> slots;
    std::vector<SlotEvidence> evidence;
    std::array<int, 7> scale {};
    std::vector<std::vector<Chord>> templateChords; // per template, reduced
    std::vector<double> templateWeights;
    double confidence = 1.0;

    Context (const HarmonicSource& s, const EngineSettings& e)
        : src (s), settings (e), slots (makeSlots (s.bars, e.rhythm)), evidence (gather (s, slots)),
          scale (theory::scalePitchClasses (s.key)),
          confidence (std::clamp (s.keyMargin / 0.3, 0.25, 1.0)) // skill: trust the prior less when the key is unsure
    {
        for (const auto& tpl : templates (e.style, s.key.mode))
        {
            std::vector<Chord> cs;
            for (const auto& r : tpl.romans)
            {
                auto d = *theory::parseRoman (r, s.key.mode);
                d.quality = reduce (d.quality, e.sevenths);
                cs.push_back (theory::chordOf (s.key, d));
            }
            templateChords.push_back (std::move (cs));
            templateWeights.push_back (tpl.weight);
        }
    }

    bool inScale (int pc) const { return std::find (scale.begin(), scale.end(), pcMod (pc)) != scale.end(); }

    /** w1 melody + w2 bass + w3 chroma for one chord in one slot (inversion included). */
    double fit (size_t slot, const Chord& c, ScoreParts* parts = nullptr) const
    {
        const auto& ev = evidence[slot];
        const auto& w = settings.weights;
        double m = 0.0, b = 0.0, ch = 0.0;
        if (ev.melodyTotal > 0.0)
        {
            for (int pc = 0; pc < 12; ++pc)
                if (ev.melody[(size_t) pc] > 0.0)
                    m += ev.melody[(size_t) pc] * (c.contains (pc) ? 1.0 : inScale (pc) ? 0.1 : -0.6);
            m /= ev.melodyTotal;
        }
        if (ev.bassTotal > 0.0)
        {
            const int bassPc = c.bassPitchClass();
            for (int pc = 0; pc < 12; ++pc)
                if (ev.bass[(size_t) pc] > 0.0)
                    b += ev.bass[(size_t) pc] * (pc == bassPc ? 1.0 : c.contains (pc) ? 0.3 : -0.5);
            b /= ev.bassTotal;
            if (c.inversion > 0)
                b *= 0.9; // prefer root position when the bass is ambiguous
        }
        if (ev.chroma)
        {
            std::array<double, 12> tmpl {};
            const auto pcs = c.pitchClasses();
            for (size_t k = 0; k < pcs.size(); ++k)
                tmpl[(size_t) pcs[k]] = k == 0 ? 1.0 : 0.8;
            double dot = 0.0, na = 0.0, nb = 0.0;
            for (size_t k = 0; k < 12; ++k)
            {
                dot += (*ev.chroma)[k] * tmpl[k];
                na += (*ev.chroma)[k] * (*ev.chroma)[k];
                nb += tmpl[k] * tmpl[k];
            }
            if (na > 0.0)
                ch = std::clamp (2.0 * (dot / std::sqrt (na * nb) - 0.5), -1.0, 1.0);
        }
        if (parts)
        {
            parts->melody += m;
            parts->bass += b;
            parts->chroma += ch;
        }
        return w.melody * m + w.bass * b + w.chroma * ch;
    }

    /** Best inversion of `c` for the slot's bass. */
    Chord withBestInversion (size_t slot, Chord c) const
    {
        c.inversion = 0;
        if (evidence[slot].bassTotal <= 0.0)
            return c;
        Chord best = c;
        double bestFit = fit (slot, c);
        for (int inv = 1; inv < (int) c.pitchClasses().size(); ++inv)
        {
            Chord x = c;
            x.inversion = inv;
            if (const double f = fit (slot, x); f > bestFit + 1e-9)
            {
                bestFit = f;
                best = x;
            }
        }
        return best;
    }

    /** Template prior for `cur` after `prev` (nullptr for the first slot): the weight of the best
        template that contains the transition (cyclically), half of it when it only contains the chord. */
    double prior (const Chord* prev, const Chord& cur) const
    {
        double best = 0.0;
        for (size_t t = 0; t < templateChords.size(); ++t)
        {
            const auto& cs = templateChords[t];
            for (size_t i = 0; i < cs.size(); ++i)
            {
                if (! sameChord (cs[i], cur))
                    continue;
                const auto& before = cs[(i + cs.size() - 1) % cs.size()];
                const double v = (prev == nullptr || sameChord (before, *prev)) ? templateWeights[t] : 0.5 * templateWeights[t];
                best = std::max (best, v);
            }
        }
        return best;
    }

    ScoreParts score (const std::vector<Chord>& chords, int* maxVl = nullptr) const
    {
        ScoreParts p;
        if (chords.empty())
            return p;
        double vlSum = 0.0;
        int vlMax = 0;
        for (size_t i = 0; i < chords.size(); ++i)
        {
            fit (i, chords[i], &p);
            p.prior += prior (i ? &chords[i - 1] : nullptr, chords[i]);
            if (i > 0)
            {
                const int vl = theory::voiceLeadingCost (chords[i - 1], chords[i]);
                vlSum += vl;
                vlMax = std::max (vlMax, vl);
            }
        }
        const double n = (double) chords.size();
        p.melody /= n;
        p.bass /= n;
        p.chroma /= n;
        p.prior = p.prior / n * confidence;
        p.voiceLeading = chords.size() > 1 ? vlSum / (n - 1) : 0.0;
        const auto& w = settings.weights;
        p.total = w.melody * p.melody + w.bass * p.bass + w.chroma * p.chroma + w.prior * p.prior - w.voiceLeading * p.voiceLeading;
        if (maxVl)
            *maxVl = vlMax;
        return p;
    }
};

uint64_t tieHash (const std::vector<Chord>& chords, uint32_t seed)
{
    uint64_t h = 1469598103934665603ULL ^ seed;
    for (const auto& c : chords)
    {
        h = (h ^ (uint64_t) (c.root * 64 + (int) c.quality * 4 + c.inversion)) * 1099511628211ULL;
    }
    return h;
}

Progression make (const Context& ctx, std::vector<Chord> chords, std::string source, double priorWeight)
{
    Progression p;
    p.slots = ctx.slots;
    p.chords = std::move (chords);
    p.source = std::move (source);
    p.priorWeight = priorWeight;
    p.score = ctx.score (p.chords, &p.maxVoiceLeading);
    return p;
}

/** Viterbi over the vocabulary: best chord sequence for fit + prior - voice leading (exact for
    the bigram prior). */
std::vector<Chord> bestFit (const Context& ctx, const std::vector<Chord>& vocab)
{
    const size_t n = ctx.slots.size(), v = vocab.size();
    if (n == 0 || v == 0)
        return {};
    const auto& w = ctx.settings.weights;
    const double vlWeight = n > 1 ? w.voiceLeading * (double) n / (double) (n - 1) : 0.0; // same scale as score()
    std::vector<std::vector<Chord>> cand (n, std::vector<Chord> (v));
    std::vector<std::vector<double>> best (n, std::vector<double> (v, -1e18));
    std::vector<std::vector<int>> from (n, std::vector<int> (v, -1));
    for (size_t i = 0; i < n; ++i)
        for (size_t k = 0; k < v; ++k)
            cand[i][k] = ctx.withBestInversion (i, vocab[k]);
    for (size_t k = 0; k < v; ++k)
        best[0][k] = ctx.fit (0, cand[0][k]) + w.prior * ctx.confidence * ctx.prior (nullptr, cand[0][k]);
    for (size_t i = 1; i < n; ++i)
        for (size_t k = 0; k < v; ++k)
        {
            const double here = ctx.fit (i, cand[i][k]);
            for (size_t j = 0; j < v; ++j)
            {
                const double s = best[i - 1][j] + here + w.prior * ctx.confidence * ctx.prior (&cand[i - 1][j], cand[i][k])
                               - vlWeight * theory::voiceLeadingCost (cand[i - 1][j], cand[i][k]);
                if (s > best[i][k])
                {
                    best[i][k] = s;
                    from[i][k] = (int) j;
                }
            }
        }
    size_t k = (size_t) (std::max_element (best[n - 1].begin(), best[n - 1].end()) - best[n - 1].begin());
    std::vector<Chord> out (n);
    for (size_t i = n; i-- > 0;)
    {
        out[i] = cand[i][k];
        if (i > 0)
            k = (size_t) from[i][k];
    }
    return out;
}
} // namespace

std::vector<std::string> Progression::romans (Key key) const
{
    std::vector<std::string> r;
    for (const auto& c : chords)
        r.push_back (theory::romanOf (key, c));
    return r;
}

std::string Progression::summary (Key key) const
{
    const auto r = romans (key);
    size_t period = std::min<size_t> (4, r.size());
    for (size_t p = 1; p <= std::min<size_t> (8, r.size()); ++p)
    {
        bool repeats = true;
        for (size_t i = p; i < r.size() && repeats; ++i)
            repeats = r[i] == r[i - p];
        if (repeats)
        {
            period = p;
            break;
        }
    }
    std::string s;
    for (size_t i = 0; i < period; ++i)
        s += (i ? " " : "") + r[i];
    return s;
}

ScoreParts scoreProgression (const HarmonicSource& src, const Progression& p, const EngineSettings& e)
{
    const Context ctx (src, e);
    return ctx.score (p.chords);
}

std::vector<Progression> suggest (const HarmonicSource& src, const EngineSettings& e)
{
    const Context ctx (src, e);
    const size_t n = ctx.slots.size();
    std::vector<Progression> all;
    if (n == 0)
        return all;

    // template candidates: every template at every rotation, tiled over the slots
    const auto& tpls = templates (e.style, src.key.mode);
    for (size_t t = 0; t < tpls.size(); ++t)
    {
        const auto& cs = ctx.templateChords[t];
        for (size_t rot = 0; rot < cs.size(); ++rot)
        {
            std::vector<Chord> chords (n);
            for (size_t i = 0; i < n; ++i)
                chords[i] = ctx.withBestInversion (i, cs[(i + rot) % cs.size()]);
            all.push_back (make (ctx, std::move (chords), tpls[t].name, tpls[t].weight));
        }
    }
    // the evidence-driven candidate (any vocabulary chord per slot)
    all.push_back (make (ctx, bestFit (ctx, vocabulary (src.key, e.style, e.sevenths)), "fit", 0.0));

    std::stable_sort (all.begin(), all.end(), [&] (const Progression& a, const Progression& b) {
        if (a.score.total != b.score.total)
            return a.score.total > b.score.total;
        return tieHash (a.chords, e.seed) < tieHash (b.chords, e.seed);
    });
    std::vector<Progression> out;
    for (auto& p : all)
    {
        if (std::any_of (out.begin(), out.end(), [&] (const Progression& q) { return q.chords == p.chords; }))
            continue;
        out.push_back (std::move (p));
        if ((int) out.size() >= e.topN)
            break;
    }
    return out;
}

std::vector<Alternative> alternatives (const HarmonicSource& src, const Progression& prog, int slot, const EngineSettings& e, int count)
{
    const Context ctx (src, e);
    std::vector<Alternative> out;
    if (slot < 0 || slot >= (int) prog.chords.size())
        return out;
    for (const auto& c : vocabulary (src.key, e.style, e.sevenths))
    {
        if (sameChord (c, prog.chords[(size_t) slot]))
            continue;
        auto chords = prog.chords;
        chords[(size_t) slot] = ctx.withBestInversion ((size_t) slot, c);
        out.push_back ({ chords[(size_t) slot], theory::romanOf (src.key, c), ctx.score (chords).total });
    }
    std::stable_sort (out.begin(), out.end(), [] (const Alternative& a, const Alternative& b) { return a.score > b.score; });
    if ((int) out.size() > count)
        out.resize ((size_t) count);
    return out;
}

std::vector<RenderedNote> render (const Progression& p, const RenderSettings& rs)
{
    std::vector<RenderedNote> notes;
    std::mt19937 rng (rs.seed);
    std::uniform_int_distribution<int> spread (-std::abs (rs.humanize), std::abs (rs.humanize));
    auto velocity = [&] { return std::clamp (rs.velocity + (rs.humanize ? spread (rng) : 0), 1, 127); };

    std::vector<int> upper;
    for (size_t i = 0; i < p.chords.size(); ++i)
    {
        const auto& c = p.chords[i];
        const auto& s = p.slots[i];
        if (upper.empty() || ! rs.voiceLead)
            upper = theory::voice (Chord { c.root, c.quality, 0 }, rs.voicing, rs.bassOctaveMidi + 12);
        else
            upper = theory::voiceLead (upper, c);
        if (rs.bassNote)
            notes.push_back ({ s.startSec, s.endSec, rs.bassOctaveMidi + pcMod (c.bassPitchClass() - rs.bassOctaveMidi), velocity(), (int) i });
        for (int pitch : upper)
            notes.push_back ({ s.startSec, s.endSec, pitch, velocity(), (int) i });
    }
    return notes;
}

} // namespace jaudios::generators
