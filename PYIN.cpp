/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */
/*
    This file is Copyright (c) 2012 Chris Cannam
  
    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use, copy,
    modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
    ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
    CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
    WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "PYIN.h"
#include "MonoNote.h"
#include "MonoPitch.h"

#include "vamp-sdk/FFT.h"

#include <vector>
#include <algorithm>

#include <cstdio>
#include <cmath>
#include <complex>

using std::string;
using std::vector;
using Vamp::RealTime;


PYIN::PYIN(float inputSampleRate) :
    Plugin(inputSampleRate),
    m_channels(0),
    m_stepSize(256),
    m_blockSize(2048),
    m_fmin(40),
    m_fmax(700),
    m_yin(2048, inputSampleRate, 0.0),
    m_oF0Candidates(0),
    m_oF0Probs(0),
    m_oVoicedProb(0),
    m_oCandidateSalience(0),
    m_oSmoothedPitchTrack(0),
    m_oNotes(0),
    m_threshDistr(2.0f),
    m_outputUnvoiced(2.0f),
    m_pitchProb(0),
    m_timestamp(0)
{
}

PYIN::~PYIN()
{
}

string
PYIN::getIdentifier() const
{
    return "pyin";
}

string
PYIN::getName() const
{
    return "pYin";
}

string
PYIN::getDescription() const
{
    return "Monophonic pitch and note tracking based on a probabilistic Yin extension.";
}

string
PYIN::getMaker() const
{
    return "Matthias Mauch";
}

int
PYIN::getPluginVersion() const
{
    // Increment this each time you release a version that behaves
    // differently from the previous one
    return 1;
}

string
PYIN::getCopyright() const
{
    return "GPL";
}

PYIN::InputDomain
PYIN::getInputDomain() const
{
    return TimeDomain;
}

size_t
PYIN::getPreferredBlockSize() const
{
    return 2048;
}

size_t 
PYIN::getPreferredStepSize() const
{
    return 256;
}

size_t
PYIN::getMinChannelCount() const
{
    return 1;
}

size_t
PYIN::getMaxChannelCount() const
{
    return 1;
}

PYIN::ParameterList
PYIN::getParameterDescriptors() const
{
    ParameterList list;
    
    ParameterDescriptor d;

    d.identifier = "threshdistr";
    d.name = "Yin threshold distribution";
    d.description = ".";
    d.unit = "";
    d.minValue = 0.0f;
    d.maxValue = 7.0f;
    d.defaultValue = 2.0f;
    d.isQuantized = true;
    d.quantizeStep = 1.0f;
    d.valueNames.push_back("Uniform");
    d.valueNames.push_back("Beta (mean 0.10)");
    d.valueNames.push_back("Beta (mean 0.15)");
    d.valueNames.push_back("Beta (mean 0.20)");
    d.valueNames.push_back("Beta (mean 0.30)");
    d.valueNames.push_back("Single Value 0.10");
    d.valueNames.push_back("Single Value 0.15");
    d.valueNames.push_back("Single Value 0.20");
    list.push_back(d);

    d.identifier = "outputunvoiced";
    d.valueNames.clear();
    d.name = "Output estimates classified as unvoiced?";
    d.description = ".";
    d.unit = "";
    d.minValue = 0.0f;
    d.maxValue = 2.0f;
    d.defaultValue = 2.0f;
    d.isQuantized = true;
    d.quantizeStep = 1.0f;
    d.valueNames.push_back("No");
    d.valueNames.push_back("Yes");
    d.valueNames.push_back("Yes, as negative frequencies");
    list.push_back(d);

    return list;
}

float
PYIN::getParameter(string identifier) const
{
    if (identifier == "threshdistr") {
            return m_threshDistr;
    }
    if (identifier == "outputunvoiced") {
            return m_outputUnvoiced;
    }
    return 0.f;
}

void
PYIN::setParameter(string identifier, float value) 
{
    if (identifier == "threshdistr")
    {
        m_threshDistr = value;
    }
    if (identifier == "outputunvoiced")
    {
        m_outputUnvoiced = value;
    }
    
}

PYIN::ProgramList
PYIN::getPrograms() const
{
    ProgramList list;
    return list;
}

string
PYIN::getCurrentProgram() const
{
    return ""; // no programs
}

void
PYIN::selectProgram(string name)
{
}

PYIN::OutputList
PYIN::getOutputDescriptors() const
{
    OutputList outputs;

    OutputDescriptor d;
    
    int outputNumber = 0;

    d.identifier = "f0candidates";
    d.name = "F0 Candidates";
    d.description = "Estimated fundamental frequency candidates.";
    d.unit = "Hz";
    d.hasFixedBinCount = false;
    // d.binCount = 1;
    d.hasKnownExtents = true;
    d.minValue = m_fmin;
    d.maxValue = 500;
    d.isQuantized = false;
    d.sampleType = OutputDescriptor::FixedSampleRate;
    d.sampleRate = (m_inputSampleRate / m_stepSize);
    d.hasDuration = false;
    outputs.push_back(d);
    m_oF0Candidates = outputNumber++;

    d.identifier = "f0probs";
    d.name = "Candidate Probabilities";
    d.description = "Probabilities  of estimated fundamental frequency candidates.";
    d.unit = "";
    d.hasFixedBinCount = false;
    // d.binCount = 1;
    d.hasKnownExtents = true;
    d.minValue = 0;
    d.maxValue = 1;
    d.isQuantized = false;
    d.sampleType = OutputDescriptor::FixedSampleRate;
    d.sampleRate = (m_inputSampleRate / m_stepSize);
    d.hasDuration = false;
    outputs.push_back(d);
    m_oF0Probs = outputNumber++;
    
    d.identifier = "voicedprob";
    d.name = "Voiced Probability";
    d.description = "Probability that the signal is voiced according to Probabilistic Yin.";
    d.unit = "";
    d.hasFixedBinCount = true;
    d.binCount = 1;
    d.hasKnownExtents = true;
    d.minValue = 0;
    d.maxValue = 1;
    d.isQuantized = false;
    d.sampleType = OutputDescriptor::FixedSampleRate;
    d.sampleRate = (m_inputSampleRate / m_stepSize);
    d.hasDuration = false;
    outputs.push_back(d);
    m_oVoicedProb = outputNumber++;

    d.identifier = "candidatesalience";
    d.name = "Candidate Salience";
    d.description = "Candidate Salience";
    d.hasFixedBinCount = true;
    d.binCount = m_blockSize / 2;
    d.hasKnownExtents = true;
    d.minValue = 0;
    d.maxValue = 1;
    d.isQuantized = false;
    d.sampleType = OutputDescriptor::FixedSampleRate;
    d.sampleRate = (m_inputSampleRate / m_stepSize);
    d.hasDuration = false;
    outputs.push_back(d);
    m_oCandidateSalience = outputNumber++;
    
    d.identifier = "smoothedpitchtrack";
    d.name = "Smoothed Pitch Track";
    d.description = ".";
    d.unit = "Hz";
    d.hasFixedBinCount = true;
    d.binCount = 1;
    d.hasKnownExtents = false;
    // d.minValue = 0;
    // d.maxValue = 1;
    d.isQuantized = false;
    d.sampleType = OutputDescriptor::FixedSampleRate;
    d.sampleRate = (m_inputSampleRate / m_stepSize);
    d.hasDuration = false;
    outputs.push_back(d);
    m_oSmoothedPitchTrack = outputNumber++;

    d.identifier = "notes";
    d.name = "Notes";
    d.description = "Derived fixed-pitch note frequencies";
    // d.unit = "MIDI unit";
    d.unit = "Hz";
    d.hasFixedBinCount = true;
    d.binCount = 1;
    d.hasKnownExtents = false;
    d.isQuantized = false;
    d.sampleType = OutputDescriptor::VariableSampleRate;
    d.sampleRate = (m_inputSampleRate / m_stepSize);
    d.hasDuration = true;
    outputs.push_back(d);
    m_oNotes = outputNumber++;

    d.identifier = "notepitchtrack";
    d.name = "Note pitch track.";
    d.description = "Estimated fundamental frequencies during notes.";
    d.unit = "Hz";
    d.hasFixedBinCount = true;
    d.binCount = 1;
    d.hasKnownExtents = true;
    d.minValue = m_fmin;
    d.maxValue = 75;
    d.isQuantized = false;
    d.sampleType = OutputDescriptor::VariableSampleRate;
    // d.sampleRate = (m_inputSampleRate / m_stepSize);
    d.hasDuration = false;
    outputs.push_back(d);
    m_oNotePitchTrack = outputNumber++;

    return outputs;
}

bool
PYIN::initialise(size_t channels, size_t stepSize, size_t blockSize)
{
    if (channels < getMinChannelCount() ||
	channels > getMaxChannelCount()) return false;

    std::cerr << "PYIN::initialise: channels = " << channels
          << ", stepSize = " << stepSize << ", blockSize = " << blockSize
          << std::endl;

    m_channels = channels;
    m_stepSize = stepSize;
    m_blockSize = blockSize;
    
    reset();

    return true;
}

void
PYIN::reset()
{    
    m_yin.setThresholdDistr(m_threshDistr);
    m_yin.setFrameSize(m_blockSize);
    
    m_pitchProb.clear();
    m_timestamp.clear();
    
    std::cerr << "PYIN::reset"
          << ", blockSize = " << m_blockSize
          << std::endl;
}

PYIN::FeatureSet
PYIN::process(const float *const *inputBuffers, RealTime timestamp)
{
    timestamp = timestamp + Vamp::RealTime::frame2RealTime(m_blockSize/4, lrintf(m_inputSampleRate));
    FeatureSet fs;
    
    double *dInputBuffers = new double[m_blockSize];
    for (size_t i = 0; i < m_blockSize; ++i) dInputBuffers[i] = inputBuffers[0][i];
    
    Yin::YinOutput yo = m_yin.processProbabilisticYin(dInputBuffers);
    
    Feature f;
    f.hasTimestamp = true;
    f.timestamp = timestamp;
    for (size_t i = 0; i < yo.freqProb.size(); ++i)
    {
        f.values.push_back(yo.freqProb[i].first);
    }
    fs[m_oF0Candidates].push_back(f);
    
    f.values.clear();
    float voicedProb = 0;
    for (size_t i = 0; i < yo.freqProb.size(); ++i)
    {
        f.values.push_back(yo.freqProb[i].second);
        voicedProb += yo.freqProb[i].second;
    }
    fs[m_oF0Probs].push_back(f);
    
    f.values.clear();
    f.values.push_back(voicedProb);
    fs[m_oVoicedProb].push_back(f);

    f.values.clear();
    float salienceSum = 0;
    for (size_t iBin = 0; iBin < yo.salience.size(); ++iBin)
    {
        f.values.push_back(yo.salience[iBin]);
        salienceSum += yo.salience[iBin];
    }
    fs[m_oCandidateSalience].push_back(f);

    delete [] dInputBuffers;

    vector<pair<double, double> > tempPitchProb;
    for (size_t iCandidate = 0; iCandidate < yo.freqProb.size(); ++iCandidate)
    {
        double tempPitch = 12 * std::log(yo.freqProb[iCandidate].first/440)/std::log(2.) + 69;
        tempPitchProb.push_back(pair<double, double>
            (tempPitch, yo.freqProb[iCandidate].second));
    }
    m_pitchProb.push_back(tempPitchProb);
        
    m_timestamp.push_back(timestamp);

    return fs;
}

PYIN::FeatureSet
PYIN::getRemainingFeatures()
{
    FeatureSet fs;
    Feature f;
    f.hasTimestamp = true;
    f.hasDuration = false;
    
    if (m_pitchProb.empty()) {
        return fs;
    }

    // MONO-PITCH STUFF
    MonoPitch mp;
    std::cerr << "before viterbi" << std::endl;
    vector<float> mpOut = mp.process(m_pitchProb);
    // std::cerr << "after viterbi " << mpOut.size() << " "<< m_timestamp.size() << std::endl;
    for (size_t iFrame = 0; iFrame < mpOut.size(); ++iFrame)
    {
        if (mpOut[iFrame] < 0 && (m_outputUnvoiced==0)) continue;
        f.timestamp = m_timestamp[iFrame];
        f.values.clear();
        if (m_outputUnvoiced == 1)
        {
            f.values.push_back(abs(mpOut[iFrame]));
        } else {
            f.values.push_back(mpOut[iFrame]);
        }
        
        fs[m_oSmoothedPitchTrack].push_back(f);
    }
    
    // MONO-NOTE STUFF
    MonoNote mn;
    std::vector<std::vector<std::pair<double, double> > > smoothedPitch;
    for (size_t iFrame = 0; iFrame < mpOut.size(); ++iFrame) {
        std::vector<std::pair<double, double> > temp;
        if (mpOut[iFrame] > 0)
        {
            double tempPitch = 12 * std::log(mpOut[iFrame]/440)/std::log(2.) + 69;
            temp.push_back(std::pair<double,double>(tempPitch, .9));
        }
        smoothedPitch.push_back(temp);
    }
    // vector<MonoNote::FrameOutput> mnOut = mn.process(m_pitchProb);
    vector<MonoNote::FrameOutput> mnOut = mn.process(smoothedPitch);
    
    // for (size_t iFrame = 0; iFrame < mnOut.size(); ++iFrame)
    // {
    //     std::cerr << mnOut[iFrame].pitch << std::endl;
    // }

    f.hasTimestamp = true;
    f.hasDuration = true;
    f.values.clear();
    f.values.push_back(0);
    
    Feature fNoteFreqTrack;
    fNoteFreqTrack.hasTimestamp = true;
    fNoteFreqTrack.hasDuration = false;
    
    int oldState = -1;
    int onsetFrame = 0;
    int framesInNote = 0;
    double pitchSum = 0;
    
    for (size_t iFrame = 0; iFrame < mnOut.size(); ++iFrame)
    {
        if (std::pow(2,(mnOut[onsetFrame].pitch - 69) / 12) * 440 >= m_fmin && mnOut[iFrame].noteState != 2 && oldState == 2)
        {
            // greedy pitch track
            vector<double> notePitchTrack;
            for (size_t i = onsetFrame; i <= iFrame; ++i) 
            {
                fNoteFreqTrack.timestamp = m_timestamp[i];
                
                bool hasPitch = 0;
                double bestProb = 0;
                double bestPitch = 0;
                
                for (int iCandidate = (int(m_pitchProb[i].size())) - 1; iCandidate != -1; --iCandidate)
                {
                    // std::cerr << "writing :( " << std::endl;
                    double tempPitch = m_pitchProb[i][iCandidate].first;
                    if (std::abs(tempPitch-mnOut[onsetFrame].pitch) < 5 && m_pitchProb[i][iCandidate].second > bestProb)
                    {
                        bestProb = m_pitchProb[i][iCandidate].second;
                        bestPitch = m_pitchProb[i][iCandidate].first;
                        hasPitch = 1;
                    }
                }
                if (hasPitch) {
                    double tempFreq = std::pow(2,(bestPitch - 69) / 12) * 440;
                    notePitchTrack.push_back(bestPitch);
                    fNoteFreqTrack.values.clear();
                    fNoteFreqTrack.values.push_back(tempFreq); // convert back to Hz (Vamp hosts prefer that)
                    fs[m_oNotePitchTrack].push_back(fNoteFreqTrack);
                }
            }
            
            // closing old note
            f.duration = m_timestamp[iFrame]-m_timestamp[onsetFrame];
            double tempPitch = mnOut[onsetFrame].pitch;
            size_t notePitchTrackSize = notePitchTrack.size();
            if (notePitchTrackSize > 2) {
                std::sort(notePitchTrack.begin(), notePitchTrack.end());
                tempPitch = notePitchTrack[notePitchTrackSize/2]; // median
            }
            f.values[0] = std::pow(2,(tempPitch - 69) / 12) * 440; // convert back to Hz (Vamp hosts prefer that)
            fs[m_oNotes].push_back(f);
            
       
        }
    
        if (mnOut[iFrame].noteState == 1 && oldState != 1)
        {
            // open note
            onsetFrame = iFrame;
            f.timestamp = m_timestamp[iFrame];
            pitchSum = 0;
            framesInNote = 0;
        }
        
        oldState = mnOut[iFrame].noteState;
    }
    
    
    return fs;
}
