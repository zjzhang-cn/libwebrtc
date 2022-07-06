/*************************************************
 * Author : Zhenjiang.Zhang
 * Email : zjzhang@126.com
 * Date : 2022-07-05
 * Description : libwebrtc example
 * License : MIT license.
 **************************************************/

#include <iostream>
#include <stdio.h>
#include <sys/time.h>
#include <condition_variable>
#include <mutex>
#include "base/scoped_ref_ptr.h"
#include "libwebrtc.h"
#include "rtc_audio_device.h"
#include "rtc_media_stream.h"
#include "rtc_media_track.h"
#include "rtc_mediaconstraints.h"
#include "rtc_peerconnection.h"
#include "rtc_peerconnection_factory.h"
#include "rtc_types.h"
#include "rtc_video_device.h"
#include "rtc_video_frame.h"
#include "rtc_video_renderer.h"

using namespace libwebrtc;


class RTCVideoRendererImpl : public RTCVideoRenderer<scoped_refptr<RTCVideoFrame>> {
 public:
  RTCVideoRendererImpl(string tag)
      : seq_(0), tag_(tag) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        tv_= tv.tv_sec * 1000 + tv.tv_usec / 1000;
      }
  virtual ~RTCVideoRendererImpl() {}
  virtual void OnFrame(scoped_refptr<RTCVideoFrame> frame) {
    seq_++;
    if (seq_ % 30 == 0) {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      ulong now = tv.tv_sec * 1000 + tv.tv_usec / 1000;
      ulong diff = now - tv_;
      tv_ = now;
      printf("[%s] Frame %dx%d %lu %f\r\n", tag_.c_string(), frame->width(), frame->height(), diff, 30000.0 / diff);
      fflush(stdout);
    }
  }
 private:
  ulong tv_;
  int seq_;
  string tag_;
};

class RTCPeerConnectionObserverImpl : public RTCPeerConnectionObserver {
  scoped_refptr<RTCPeerConnection> pc_;
  string tags_;
 public:
  RTCPeerConnectionObserverImpl(string tags, scoped_refptr<RTCPeerConnection> pc)
      : pc_(pc), tags_(tags){};
  virtual void OnSignalingState(RTCSignalingState state) {
    printf("==%s==>\tOnSignalingState %d\r\n", tags_.c_string(), state);
    fflush(stdout);
  };

  virtual void OnPeerConnectionState(RTCPeerConnectionState state) {
    printf("==%s==>\tOnPeerConnectionState %d\r\n", tags_.c_string(), state);
    fflush(stdout);
  };

  virtual void OnIceGatheringState(RTCIceGatheringState state) {
    printf("==%s==>\tOnIceGatheringState %d\r\n", tags_.c_string(), state);
    fflush(stdout);
  };

  virtual void OnIceConnectionState(RTCIceConnectionState state) {
    printf("==%s==>\tOnIceConnectionState %d\r\n", tags_.c_string(), state);
    fflush(stdout);
  };

  virtual void OnIceCandidate(scoped_refptr<RTCIceCandidate> candidate) {
    printf("==%s==>\tOnIceCandidate %s\r\n", tags_.c_string(), candidate->candidate().c_string());
    pc_->AddCandidate(candidate->sdp_mid(), candidate->sdp_mline_index(), candidate->candidate());
    fflush(stdout);
  };

  virtual void OnAddStream(scoped_refptr<RTCMediaStream> stream) {
    auto video = stream->video_tracks();
    auto audio = stream->audio_tracks();
    printf("==%s==>\tOnAddStream Video %lu\r\n", tags_.c_string(), video.size());
    printf("==%s==>\tOnAddStream Audio %lu\r\n", tags_.c_string(), audio.size());
    for (scoped_refptr<RTCVideoTrack> track : video.std_vector()) {
      printf("==%s==>\tOnAddStream RTCVideoTrack %s\r\n", tags_.c_string(), track->id().c_string());
      track->AddRenderer(new RTCVideoRendererImpl(tags_.std_string()+"\t"+ track->id().std_string()));
    }
    for (scoped_refptr<RTCAudioTrack> track : audio.std_vector()) {
      printf("==%s==>\tOnAddStream RTCAudioTrack %s\r\n", tags_.c_string(), track->id().c_string());
    }
    fflush(stdout);
  };

  virtual void OnRemoveStream(scoped_refptr<RTCMediaStream> stream) {
    printf("==%s==>OnRemoveStream\r\n", tags_.c_string());
    fflush(stdout);
  };

  virtual void OnDataChannel(scoped_refptr<RTCDataChannel> data_channel) {
    printf("==%s==>\tOnDataChannel\r\n", tags_.c_string());
    fflush(stdout);
  };

  virtual void OnRenegotiationNeeded() {
    printf("==%s==>\tOnRenegotiationNeeded\r\n", tags_.c_string());
    fflush(stdout);
  };

  virtual void OnTrack(scoped_refptr<RTCRtpTransceiver> transceiver) {
    printf("==%s==>\tOnTrack mid %s direction %d \r\n", tags_.c_string(), transceiver->mid().c_string(),transceiver->direction());
    fflush(stdout);
  };

  virtual void OnAddTrack(vector<scoped_refptr<RTCMediaStream>> streams,
                          scoped_refptr<RTCRtpReceiver> receiver) {
    printf("==%s==>\tOnAddTrack MediaType %d %s\r\n", tags_.c_string(), receiver->media_type(), receiver->track()->id().c_string());
    fflush(stdout);
  };

  virtual void OnRemoveTrack(scoped_refptr<RTCRtpReceiver> receiver) {
    printf("==%s==>\tOnRemoveTrack\r\n", tags_.c_string());
    fflush(stdout);
  };
};

RTCConfiguration config_;

typedef void (RTCPeerConnection::*CreateFn) (
    OnSdpCreateSuccess,
    OnSdpCreateFailure,
    scoped_refptr<RTCMediaConstraints>);

int waitCreateDescription(scoped_refptr<RTCPeerConnection> pc,CreateFn fn, scoped_refptr<RTCMediaConstraints> mc, string &sdp,string &type,string &error) {
  int ret = -1;
  bool resultReady = false;
  std::mutex m;
  std::unique_lock<std::mutex> k(m);
  std::condition_variable c;
  (pc->*fn)(
      [&](const libwebrtc::string sdp_, const libwebrtc::string type_) {
        ret=0;
        sdp=sdp_;
        type=type_;
        resultReady = true;
        c.notify_one(); },
      [&](const char* error_) {
        error = string(error_);
        resultReady = true;
        c.notify_one();
      },
      mc);
  c.wait(k, [&] { return resultReady; });
  return ret;
}

typedef void (RTCPeerConnection::*SetLocalFn) (
    string,
    string,
    OnSetSdpSuccess,
    OnSetSdpFailure);
int waitSetDescription(scoped_refptr<RTCPeerConnection> pc,SetLocalFn fn,string sdp,string type,string &error) {
  int  ret= -1;
  bool resultReady = false;
  std::mutex m;
  std::unique_lock<std::mutex> k(m);
  std::condition_variable c;
  (pc->*fn)(sdp,type,
      [&]() {
        ret=0;
        resultReady = true;
        c.notify_one(); },
      [&](const char* error_) {
        error = string(error_);
        resultReady = true;
        c.notify_one();
      });
  c.wait(k, [&] { return resultReady; });
  return ret;
}
typedef void (RTCPeerConnection::*GetLocalFn) (
  OnGetSdpSuccess,
  OnGetSdpFailure);

int waitGetDescription(scoped_refptr<RTCPeerConnection> pc,GetLocalFn fn,string &sdp,string &type,string &error) {
  int  ret= -1;
  bool resultReady = false;
  std::mutex m;
  std::unique_lock<std::mutex> k(m);
  std::condition_variable c;
  (pc->*fn)(
      [&](const char* sdp_, const char* type_) {
        ret=0;
        sdp=string(sdp_);
        type=string(type_);
        resultReady = true;
        c.notify_one(); },
      [&](const char* error_) {
        error=string(error_);
        resultReady = true;
        c.notify_one();
      });
  c.wait(k, [&] { return resultReady; });
  return ret;
}


int main() {
  LibWebRTC::Initialize();
  config_.sdp_semantics = SdpSemantics::kUnifiedPlan;
  scoped_refptr<RTCPeerConnectionFactory> pcFactory = LibWebRTC::CreateRTCPeerConnectionFactory();
  pcFactory->Initialize();
  //屏幕设备测试
  auto screen_device_ = pcFactory->GetDesktopDevice();
  auto video_screen_ = screen_device_->CreateScreenCapturer();
  auto video_window_ = screen_device_->CreateWindowCapturer();

  //摄像设备测试
  auto video_device_ = pcFactory->GetVideoDevice();
  int cnum=video_device_->NumberOfDevices();
  printf(" Number Of Video Devices %d \r\n", cnum);
  char deviceNameUTF8[255];
  char deviceUniqueIdUTF8[255];
  for(int i=0;i<cnum;i++){
    video_device_->GetDeviceName(i, deviceNameUTF8, 254, deviceUniqueIdUTF8, 254);
    printf(" Name Of Video Devices [%s] [%s]\r\n", deviceNameUTF8, deviceUniqueIdUTF8);
  }

  //摄像设备捕获
  auto video_caputer_ = video_device_->Create(deviceNameUTF8, 0, 640, 480, 30);
  auto constraints = RTCMediaConstraints::Create();
  auto video_source_ = pcFactory->CreateVideoSource(video_caputer_, "Test", constraints);
  // auto video_track_ = pcFactory->CreateVideoTrack(video_source_, "Video_Test0");
  // video_track_->AddRenderer(new RTCVideoRendererImpl("Local Renderer"));

  //音频设备测试
  auto audio_device_ = pcFactory->GetAudioDevice();
  int rnum=audio_device_->RecordingDevices();
  int pnum=audio_device_->PlayoutDevices();
  printf(" Number Of Audio Recording Devices %d \r\n",rnum);
  printf(" Number Of Audio Playout Devices %d \r\n", pnum);
  char name[RTCAudioDevice::kAdmMaxDeviceNameSize];
  char guid[RTCAudioDevice::kAdmMaxGuidSize];
  for (int i=0;i<rnum;i++){
    audio_device_->RecordingDeviceName(i, name, guid);
    printf(" Name Of Audio Recording Devices [%s] [%s] \r\n", name, guid);
  }
  for (int i=0;i<pnum;i++){
    audio_device_->PlayoutDeviceName(i, name, guid);
    printf(" Name Of Audio Playout Devices [%s] [%s] \r\n", name, guid);
  }
  audio_device_->SetPlayoutDevice(0);
  audio_device_->SetRecordingDevice(0);
  //音频设备捕获
  auto audio_source_ = pcFactory->CreateAudioSource("Test");
  auto audio_track_ = pcFactory->CreateAudioTrack(audio_source_, "Audio_Test");

  auto pc_mc = RTCMediaConstraints::Create();
  pc_mc->AddMandatoryConstraint(RTCMediaConstraints::kEnableIPv6,"false");
  pc_mc->AddOptionalConstraint(RTCMediaConstraints::kEnableIPv6,"false");
  pc_mc->AddMandatoryConstraint(RTCMediaConstraints::kIceRestart,"true");
  auto pc_sender = pcFactory->Create(config_, pc_mc);
  auto pc_receiver = pcFactory->Create(config_, pc_mc);
  // 1.使用AddStream方式添加媒体(过时)
#if 0  
  auto stream_ = pcFactory->CreateStream("Test");
  stream_->AddTrack(pcFactory->CreateAudioTrack(audio_source_, "Audio_Test1"));
  stream_->AddTrack(pcFactory->CreateAudioTrack(audio_source_, "Audio_Test2"));
  stream_->AddTrack(pcFactory->CreateAudioTrack(audio_source_, "Audio_Test3"));
  stream_->AddTrack(pcFactory->CreateAudioTrack(audio_source_, "Audio_Test4"));
  stream_->AddTrack(video_track_);
  pc_sender->AddStream(stream_);
  pc_receiver->AddStream(stream_);
#endif
  // 2.使用AddTransceiver添加媒体

  std::vector<std::string> stream_ids({"Test"});
  libwebrtc::scoped_refptr<libwebrtc::RTCRtpEncodingParameters> encoding =
      RTCRtpEncodingParameters::Create();

  encoding->set_active(true);
  encoding->set_max_bitrate_bps(500000);
  encoding->set_min_bitrate_bps(300000);
  encoding->set_max_framerate(15);  
  encoding->set_scale_resolution_down_by(1.0);
  //encoding->set_rid();
  //encoding->set_ssrc();
  //encoding->set_num_temporal_layers();
  //encoding->set_scale_resolution_down_by();
  std::vector<scoped_refptr<RTCRtpEncodingParameters>> encodings;
  encodings.push_back(encoding);
  auto init=RTCRtpTransceiverInit::Create(RTCRtpTransceiverDirection::kSendOnly,stream_ids,encodings);
  //pc_sender->AddTransceiver(RTCMediaType::VIDEO,init);
  auto trans = pc_sender->AddTransceiver(pcFactory->CreateVideoTrack(video_source_, "Video_Test1"),init);
  // auto trans = pc_sender->AddTransceiver(pcFactory->CreateVideoTrack(video_source_, "Video_Test1"));  
  //pc_receiver->AddTransceiver(RTCMediaType::VIDEO);
  pc_receiver->AddTransceiver(RTCMediaType::VIDEO);

  // 3.使用AddTrack方式添加媒体
#if 0
  std::vector<std::string> stream_ids({"Test"});
  pc_sender->AddTrack(pcFactory->CreateAudioTrack(audio_source_, "Audio_Test1"),stream_ids);
  pc_sender->AddTrack(pcFactory->CreateAudioTrack(audio_source_, "Audio_Test2"),stream_ids);
  pc_sender->AddTrack(pcFactory->CreateAudioTrack(audio_source_, "Audio_Test3"),stream_ids);
  pc_sender->AddTrack(pcFactory->CreateAudioTrack(audio_source_, "Audio_Test4"),stream_ids);
  auto v_sender=pc_sender->AddTrack(pcFactory->CreateVideoTrack(video_source_, "Video_Test1"),stream_ids);
  auto param=v_sender->parameters();
  auto encodings=v_sender->init_send_encodings();
   std::cout << "==> encodings size:" <<encodings.size() <<std::endl;
  for (scoped_refptr<RTCRtpEncodingParameters> encoding : encodings.std_vector()) {
    std::cout << "==> min_bitrate_bps " <<encoding->min_bitrate_bps() <<std::endl;
    std::cout << "==> max_bitrate_bps " <<encoding->max_bitrate_bps() <<std::endl;
    std::cout << "==> max_framerate " <<encoding->max_framerate() <<std::endl;
  }
  pc_sender->AddTrack(pcFactory->CreateVideoTrack(video_source_, "Video_Test2"),stream_ids);
  pc_receiver->AddTrack(pcFactory->CreateAudioTrack(audio_source_, "Audio_Test1"),stream_ids);
  pc_receiver->AddTrack(pcFactory->CreateVideoTrack(video_source_, "Video_Test1"),stream_ids);
#endif


  //设置状态回调
  pc_receiver->RegisterRTCPeerConnectionObserver(new RTCPeerConnectionObserverImpl("answer", pc_sender));
  pc_sender->RegisterRTCPeerConnectionObserver(new RTCPeerConnectionObserverImpl("offer", pc_receiver));

  std::cout<<std::endl << "=========          BEGIN SWITCH SDP && ICE          ========"<<std::endl<<std::endl;
  string o_sdp,o_type,error;
  string a_sdp,a_type;
  int ret=0;
  ret=waitCreateDescription(pc_sender,&RTCPeerConnection::CreateOffer, pc_mc, o_sdp,o_type,error);
  if (ret<0){
    printf("+++ pc_sender\tCreateOffer ERR:%sr\n", error.c_string());
  }else{
    printf("+++ pc_sender\tCreateOfferr\n");
  }

  ret=waitSetDescription(pc_receiver,&RTCPeerConnection::SetRemoteDescription,o_sdp,o_type,error);
  if (ret<0){
    printf("+++ pc_receiver\tSetRemoteDescription ERR:%s\r\n", error.c_string());
  }else{
    printf("+++ pc_receiver\tSetRemoteDescription \r\n");
  }
  ret=waitCreateDescription(pc_receiver,&RTCPeerConnection::CreateAnswer, pc_mc, a_sdp,a_type,error);
  if (ret<0){
    printf("+++ pc_receiver\tCreateAnswer ERR:%s \r\n", error.c_string());
  }else{
    printf("+++ pc_receiver\tCreateAnswer \r\n");
  }

  //通过SetLocalDescription开始获取ICE

  ret=waitSetDescription(pc_receiver,&RTCPeerConnection::SetLocalDescription,a_sdp,a_type,error);  
  if (ret<0){
    printf("+++ pc_receiver\tSetLocalDescription ERR:%s \r\n", error.c_string());
  }else{
    printf("+++ pc_receiver\tSetLocalDescription \r\n");
  }


  ret=waitSetDescription(pc_sender,&RTCPeerConnection::SetLocalDescription,o_sdp,o_type,error);
  if (ret<0){
    printf("+++ pc_sender\tSetLocalDescription ERR:%s \r\n", error.c_string());
  }else{
    printf("+++ pc_sender\tSetLocalDescription \r\n");
  }

  ret=waitSetDescription(pc_sender,&RTCPeerConnection::SetRemoteDescription,a_sdp,a_type,error);
  if (ret<0){
    printf("+++ pc_sender\tSetRemoteDescription ERR:%s \r\n", error.c_string());
  }else{
    printf("+++ pc_sender\tSetRemoteDescription \r\n");
  }



  ret=waitGetDescription(pc_receiver,&RTCPeerConnection::GetLocalDescription,a_sdp,a_type,error);  
  if (ret<0){
    printf("+++ pc_receiver\tGetLocalDescription ERR:%s \r\n", error.c_string());
  }else{
    printf("+++ pc_receiver\tGetLocalDescription \r\n%s",a_sdp.c_string());
  }

  ret=waitGetDescription(pc_sender,&RTCPeerConnection::GetLocalDescription,o_sdp,o_type,error);
  if (ret<0){
    printf("+++ pc_sender\tGetLocalDescription ERR:%s \r\n", error.c_string());
  }else{
    printf("+++ pc_sender\tGetLocalDescription \r\n%s",o_sdp.c_string());
  }


  // pc_sender->RestartIce();

  getchar();
  pc_sender->Close();
  pc_receiver->Close();
  printf("Fininsh\r\n");
  audio_device_ = nullptr;
  video_device_ = nullptr;
  video_screen_ = nullptr;
  video_window_ = nullptr;
  pcFactory->Terminate();
}