/*
Copyright (C) 2007 Christian Kothe, Mark Spieth

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef FREESURROUND_H
#define FREESURROUND_H

#include "compat.h"  // instead of sys/types.h, for MinGW compatibility

#define SURROUND_BUFSIZE 8192

class FreeSurround
{
public:
    enum SurroundMode
    {
        SurroundModePassive,
        SurroundModeActiveSimple,
        SurroundModeActiveLinear,
        SurroundModePassiveHall
    };
public:
    FreeSurround(uint srate, bool moviemode, SurroundMode mode);
    ~FreeSurround();

    // put frames in buffer, returns number of frames used
    uint putFrames(void* buffer, uint numFrames, uint numChannels);
    // get a number of frames
    uint receiveFrames(void *buffer, uint maxFrames);
    // flush unprocessed samples
    void flush();
    uint numUnprocessedFrames();
    uint numFrames();

    long long getLatency();
    uint frameLatency();

    static uint framesPerBlock();

protected:
    void process_block();
    void open();
    void close();
    void SetParams();

private:

    // the changeable parameters
    struct fsurround_params {
        int32_t center_width;           // presence of the center channel
        int32_t dimension;              // dimension
        float coeff_a,coeff_b;          // surround mixing coefficients
        int32_t phasemode;              // phase shifting mode
        int32_t steering;               // steering mode (0=simple, 1=linear)
        int32_t front_sep, rear_sep;    // front/rear stereo separation
        // (default) constructor
        fsurround_params(int32_t center_width=100, int32_t dimension=0);
    } m_params;

    // additional settings
    uint m_srate;

    // info about the current setup
    struct buffers          *m_bufs    {nullptr}; // our buffers
    class fsurround_decoder *m_decoder {nullptr}; // the surround decoder
    int  m_inCount                     {0};       // amount in lt,rt
    int  m_outCount                    {0};       // amount in output bufs
    bool m_processed                   {true};    // whether processing is enabled for latency calc
    int  m_processedSize               {0};       // amount processed
    SurroundMode m_surroundMode {SurroundModePassive}; // 1 of 3 surround modes supported
    int m_latencyFrames                {0};       // number of frames of incurred latency
    int m_channels                     {0};
};

#endif
