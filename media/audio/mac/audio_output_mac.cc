// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/audio/mac/audio_manager_mac.h"
#include "media/audio/mac/audio_output_mac.h"

#include "base/basictypes.h"
#include "base/logging.h"

PCMQueueOutAudioOutputStream::PCMQueueOutAudioOutputStream(
    AudioManagerMac* manager, int channels, int sampling_rate,
    char bits_per_sample)
        : format_(),
          audio_queue_(NULL),
          buffer_(),
          source_(NULL),
          manager_(manager) {
  // We must have a manager.
  DCHECK(manager_);
  // A frame is one sample across all channels. In interleaved audio the per
  // frame fields identify the set of n |channels|. In uncompressed audio, a
  // packet is always one frame.
  format_.mSampleRate = sampling_rate;
  format_.mFormatID = kAudioFormatLinearPCM;
  format_.mFormatFlags = kLinearPCMFormatFlagIsPacked |
                         kLinearPCMFormatFlagIsSignedInteger;
  format_.mBitsPerChannel = bits_per_sample;
  format_.mBytesPerFrame = format_.mBytesPerPacket;
  format_.mChannelsPerFrame = channels;
  format_.mFramesPerPacket = 1;
  format_.mBytesPerPacket = (format_.mBitsPerChannel * channels) / 8;
}

PCMQueueOutAudioOutputStream::~PCMQueueOutAudioOutputStream() {
}

void PCMQueueOutAudioOutputStream::HandleError(OSStatus err) {
  if (source_)
    source_->OnError(this, static_cast<int>(err));
  NOTREACHED() << "error code " << err;
}

bool PCMQueueOutAudioOutputStream::Open(size_t packet_size) {
  if (0 == packet_size) {
    // TODO(cpu) : Impelement default buffer computation.
    return false;
  }
  // Create the actual queue object and let the OS use its own thread to
  // run its CFRunLoop.
  OSStatus err = AudioQueueNewOutput(&format_, RenderCallback, this, NULL,
                                     kCFRunLoopCommonModes, 0, &audio_queue_);
  if (err != noErr) {
    HandleError(err);
    return false;
  }
  // Allocate the hardware-managed buffers.
  for(size_t ix = 0; ix != kNumBuffers; ++ix) {
    err = AudioQueueAllocateBuffer(audio_queue_, packet_size, &buffer_[ix]);
    if (err != noErr) {
      HandleError(err);
      return false;
    }
  }
  // Set initial volume here.
  err = AudioQueueSetParameter(audio_queue_, kAudioQueueParam_Volume, 1.0);
  if (err != noErr) {
    HandleError(err);
    return false;
  }
  return true;
}

void PCMQueueOutAudioOutputStream::Close() {
  // It is valid to call Close() before calling Open(), thus audio_queue_
  // might be NULL.
  if (audio_queue_) {
    OSStatus err = 0;
    for (size_t ix = 0; ix != kNumBuffers; ++ix) {
      if (buffer_[ix]) {
        err = AudioQueueFreeBuffer(audio_queue_, buffer_[ix]);
        if (err) {
          HandleError(err);
          break;
        }
      }
    }
    err = AudioQueueDispose(audio_queue_, true);
    if (err) {
      HandleError(err);
    }
  }
  // Inform the audio manager that we have been closed. This can cause our
  // destruction.
  manager_->ReleaseStream(this);
}

void PCMQueueOutAudioOutputStream::Stop() {
  // TODO(cpu): Implement.
}

void PCMQueueOutAudioOutputStream::SetVolume(double left_level,
                                             double right_level) {
  // TODO(cpu): Implement.
}

void PCMQueueOutAudioOutputStream::GetVolume(double* left_level,
                                             double* right_level) {
  // TODO(cpu): Implement.
}

size_t PCMQueueOutAudioOutputStream::GetNumBuffers() {
  return kNumBuffers;
}

void PCMQueueOutAudioOutputStream::RenderCallback(void* p_this,
                                                  AudioQueueRef queue,
                                                  AudioQueueBufferRef buffer) {
  PCMQueueOutAudioOutputStream* audio_stream =
      static_cast<PCMQueueOutAudioOutputStream*>(p_this);
  // Call the audio source to fill the free buffer with data.
  size_t capacity = buffer->mAudioDataBytesCapacity;
  size_t filled = audio_stream->source_->OnMoreData(audio_stream, 
                                                    buffer->mAudioData,
                                                    capacity);
  if (filled > capacity) {
    // User probably overran our buffer.
    audio_stream->HandleError(0);
    return;
  }
  // Queue the audio data to the audio driver.
  buffer->mAudioDataByteSize = filled;
  OSStatus err = AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
  if (err != noErr) 
    audio_stream->HandleError(err);
}

void PCMQueueOutAudioOutputStream::Start(AudioSourceCallback* callback) {
  OSStatus err = AudioQueueStart(audio_queue_, NULL);
  if (err != noErr) {
    HandleError(err);
    return;
  }
  // TODO(cpu) : Prefill the avaiable buffers.
}
