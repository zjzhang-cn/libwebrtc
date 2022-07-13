/*************************************************
 * Author : Zhenjiang.Zhang
 * Email : zjzhang@126.com
 * Date : 2022-07-05
 * Description : libwebrtc example
 * License : MIT license.
 **************************************************/

#include <stdio.h>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>  // std::thread
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
#ifdef WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif

using namespace libwebrtc;
std::map<std::string, scoped_refptr<RTCMediaTrack>> localTracks_;
//交换SDP的方法
int exchangeDescription(
    scoped_refptr<libwebrtc::RTCPeerConnection> pc_sender,
    scoped_refptr<libwebrtc::RTCPeerConnection> pc_receiver);

//同步回调的声明
typedef void (RTCPeerConnection::*CreateFn)(OnSdpCreateSuccess,
                                            OnSdpCreateFailure,
                                            scoped_refptr<RTCMediaConstraints>);

int waitCreateDescription(scoped_refptr<RTCPeerConnection> pc,
                          CreateFn fn,
                          scoped_refptr<RTCMediaConstraints> mc,
                          string& sdp,
                          string& type,
                          string& error) {
  int ret = -1;
  bool resultReady = false;
  std::mutex m;
  std::unique_lock<std::mutex> k(m);
  std::condition_variable c;
  (pc->*fn)(
      [&](const libwebrtc::string sdp_, const libwebrtc::string type_) {
        ret = 0;
        sdp = sdp_;
        type = type_;
        resultReady = true;
        c.notify_one();
      },
      [&](const char* error_) {
        error = string(error_);
        resultReady = true;
        c.notify_one();
      },
      mc);
  c.wait(k, [&] { return resultReady; });
  return ret;
}

typedef void (RTCPeerConnection::*SetLocalFn)(string,
                                              string,
                                              OnSetSdpSuccess,
                                              OnSetSdpFailure);
int waitSetDescription(scoped_refptr<RTCPeerConnection> pc,
                       SetLocalFn fn,
                       string sdp,
                       string type,
                       string& error) {
  int ret = -1;
  bool resultReady = false;
  std::mutex m;
  std::unique_lock<std::mutex> k(m);
  std::condition_variable c;
  (pc->*fn)(
      sdp, type,
      [&]() {
        ret = 0;
        resultReady = true;
        c.notify_one();
      },
      [&](const char* error_) {
        error = string(error_);
        resultReady = true;
        c.notify_one();
      });
  c.wait(k, [&] { return resultReady; });
  return ret;
}
typedef void (RTCPeerConnection::*GetLocalFn)(OnGetSdpSuccess, OnGetSdpFailure);

int waitGetDescription(scoped_refptr<RTCPeerConnection> pc,
                       GetLocalFn fn,
                       string& sdp,
                       string& type,
                       string& error) {
  int ret = -1;
  bool resultReady = false;
  std::mutex m;
  std::unique_lock<std::mutex> k(m);
  std::condition_variable c;
  (pc->*fn)(
      [&](const char* sdp_, const char* type_) {
        ret = 0;
        sdp = string(sdp_);
        type = string(type_);
        resultReady = true;
        c.notify_one();
      },
      [&](const char* error_) {
        error = string(error_);
        resultReady = true;
        c.notify_one();
      });
  c.wait(k, [&] { return resultReady; });
  return ret;
}

//视频回显的实现
class RTCVideoRendererImpl : public RTCVideoRenderer<scoped_refptr<RTCVideoFrame>> {
 public:
  RTCVideoRendererImpl(string tag)
      : tag_(tag) {
    printf("[%s] RTCVideoRendererImpl\r\n", tag_.c_string());
  }
  virtual ~RTCVideoRendererImpl() {}
  virtual void OnFrame(scoped_refptr<RTCVideoFrame> frame) {
    // if (w != frame->width() || h != frame->height()) {
    printf("[%s] Frame %dx%d\r\n", tag_.c_string(), frame->width(), frame->height());
    w = frame->width();
    h = frame->height();
    //}
  }

 private:
  string tag_;
  int w, h;
};

// DataChannel的回调实现
class RTCDataChannelObserverImpl : public RTCDataChannelObserver {
 public:
  RTCDataChannelObserverImpl(scoped_refptr<RTCDataChannel> dc)
      : dc_(dc) {}
  virtual void OnStateChange(RTCDataChannelState state) {
    std::cout << "Answer RTCDataChannel [" << dc_->label().c_string()
              << "]OnStateChange :" << state << std::endl;
  }
  virtual void OnMessage(const char* buffer, int length, bool binary) {
    auto msg = string(buffer);
    std::cout << "Answer RTCDataChannel[" << dc_->label().c_string()
              << "]OnMessage [" << buffer << "] len:" << length << std::endl;
    dc_->Send((const uint8_t*)msg.c_string(), msg.size() + 1, false);
  }

 private:
  scoped_refptr<RTCDataChannel> dc_;
};

class TrackStatsObserverImpl : public TrackStatsObserver {
 public:
  void OnComplete(const MediaTrackStatistics& reports) {
    std::cout << "bytes_received " << reports.bytes_received << std::endl;
  }
};

// PeerConnection的回调实现
class RTCPeerConnectionObserverImpl : public RTCPeerConnectionObserver {
  scoped_refptr<RTCPeerConnection> pc_;
  scoped_refptr<RTCPeerConnection> other_;
  string tags_;

 public:
  RTCPeerConnectionObserverImpl(string tags,
                                scoped_refptr<RTCPeerConnection> pc,
                                scoped_refptr<RTCPeerConnection> other)
      : pc_(pc), other_(other), tags_(tags) {}
  virtual void OnSignalingState(RTCSignalingState state) {
    printf("==%s==>\tOnSignalingState %d\r\n", tags_.c_string(), state);
    fflush(stdout);
  }

  virtual void OnPeerConnectionState(RTCPeerConnectionState state) {
    printf("==%s==>\tOnPeerConnectionState %d\r\n", tags_.c_string(), state);
    fflush(stdout);
  }

  virtual void OnIceGatheringState(RTCIceGatheringState state) {
    printf("==%s==>\tOnIceGatheringState %d\r\n", tags_.c_string(), state);
    fflush(stdout);
  }

  virtual void OnIceConnectionState(RTCIceConnectionState state) {
    printf("==%s==>\tOnIceConnectionState %d\r\n", tags_.c_string(), state);
    fflush(stdout);
  }

  virtual void OnIceCandidate(scoped_refptr<RTCIceCandidate> candidate) {
    printf("==%s==>\tOnIceCandidate %s\r\n", tags_.c_string(),
           candidate->candidate().c_string());
    other_->AddCandidate(candidate->sdp_mid(), candidate->sdp_mline_index(), candidate->candidate());
    fflush(stdout);
  }

  virtual void OnAddStream(scoped_refptr<RTCMediaStream> stream) {
    printf("==%s==> OnAddStream \r\n", tags_.c_string());
    fflush(stdout);
  }

  virtual void OnRemoveStream(scoped_refptr<RTCMediaStream> stream) {
    printf("==%s==> OnRemoveStream\r\n", tags_.c_string());
    fflush(stdout);
  }

  virtual void OnDataChannel(scoped_refptr<RTCDataChannel> data_channel) {
    data_channel->RegisterObserver(new RTCDataChannelObserverImpl(data_channel));
    printf("==%s==>\t OnDataChannel\r\n", tags_.c_string());
    fflush(stdout);
  }

  virtual void OnRenegotiationNeeded() {
    printf("==%s==>\tOnRenegotiationNeeded\r\n", tags_.c_string());
    //重新协商
    // 此处使用线程，因为在exchangeDescription方法中会阻塞，并等待CreateOffer的回调
    // 但是由于OnRenegotiationNeeded
    // 也在WebRTC的信号线程中，所以CreateOffer的回调不会返回。
    auto t = std::thread(exchangeDescription, pc_, other_);
    t.detach();
  }

  virtual void OnTrack(scoped_refptr<RTCRtpTransceiver> transceiver) {
    printf("==%s==>\tOnTrack mid %s direction %d \r\n", tags_.c_string(),
           transceiver->mid().c_string(), transceiver->direction());
    fflush(stdout);
  }

  virtual void OnAddTrack(vector<scoped_refptr<RTCMediaStream>> streams,
                          scoped_refptr<RTCRtpReceiver> receiver) {
    auto track = receiver->track();

    printf("==%s==>\t OnAddTrack MediaType %d %s %s\r\n", tags_.c_string(),
           receiver->media_type(), receiver->track()->kind().c_string(),
           track->id().c_string());
    //!!! 必須保存
    localTracks_.insert(std::pair<std::string, scoped_refptr<RTCMediaTrack>>(track->kind().std_string(), track));
    if (track->kind().std_string() == "video") {
      ((RTCVideoTrack*)track.get())->AddRenderer(new RTCVideoRendererImpl(tags_.std_string() + "\t" + track->id().std_string()));
    }
  }

  virtual void OnRemoveTrack(scoped_refptr<RTCRtpReceiver> receiver) {
    auto track = receiver->track();
    localTracks_.erase(track->id().std_string());
    printf("==%s==>\tOnRemoveTrack\r\n", tags_.c_string());
    fflush(stdout);
  }
};

int exchangeDescription(
    scoped_refptr<libwebrtc::RTCPeerConnection> pc_sender,
    scoped_refptr<libwebrtc::RTCPeerConnection> pc_receiver) {
  std::cout << std::endl
            << "=========          Exchange Description         ========"
            << std::endl
            << std::endl;
  auto pc_mc = RTCMediaConstraints::Create();
  // pc_mc->AddMandatoryConstraint("IceRestart","true");
  string o_sdp, o_type, error;
  string a_sdp, a_type;
  int ret = 0;
  //创建Offer
  ret = waitCreateDescription(pc_sender, &RTCPeerConnection::CreateOffer, pc_mc,
                              o_sdp, o_type, error);
  if (ret < 0) {
    printf("+++ pc_sender\tCreateOffer ERR:%sr\n", error.c_string());
  } else {
    printf("+++ pc_sender\tCreateOfferr\n");
  }
  //设置Offer
  ret = waitSetDescription(pc_receiver, &RTCPeerConnection::SetRemoteDescription, o_sdp, o_type, error);
  if (ret < 0) {
    printf("+++ pc_receiver\tSetRemoteDescription ERR:%s\r\n",
           error.c_string());
  } else {
    printf("+++ pc_receiver\tSetRemoteDescription \r\n");
  }
  //创建Answer
  ret = waitCreateDescription(pc_receiver, &RTCPeerConnection::CreateAnswer, pc_mc, a_sdp, a_type, error);
  if (ret < 0) {
    printf("+++ pc_receiver\tCreateAnswer ERR:%s \r\n", error.c_string());
  } else {
    printf("+++ pc_receiver\tCreateAnswer \r\n");
  }

  //通过SetLocalDescription开始获取ICE
  //设置本地描述
  ret = waitSetDescription(pc_receiver, &RTCPeerConnection::SetLocalDescription, a_sdp, a_type, error);
  if (ret < 0) {
    printf("+++ pc_receiver\tSetLocalDescription ERR:%s \r\n",
           error.c_string());
  } else {
    printf("+++ pc_receiver\tSetLocalDescription \r\n");
  }
  ret = waitSetDescription(pc_sender, &RTCPeerConnection::SetLocalDescription, o_sdp, o_type, error);
  if (ret < 0) {
    printf("+++ pc_sender\tSetLocalDescription ERR:%s \r\n", error.c_string());
  } else {
    printf("+++ pc_sender\tSetLocalDescription \r\n");
  }
  //设置Answer
  ret = waitSetDescription(pc_sender, &RTCPeerConnection::SetRemoteDescription, a_sdp, a_type, error);
  if (ret < 0) {
    printf("+++ pc_sender\tSetRemoteDescription ERR:%s \r\n", error.c_string());
  } else {
    printf("+++ pc_sender\tSetRemoteDescription \r\n");
  }
  //获取本地描述
  ret = waitGetDescription(pc_receiver, &RTCPeerConnection::GetLocalDescription, a_sdp, a_type, error);
  if (ret < 0) {
    printf("+++ pc_receiver\tGetLocalDescription ERR:%s \r\n",
           error.c_string());
  } else {
    printf("+++ pc_receiver\tGetLocalDescription \r\n");
    // printf("+++ pc_receiver\tGetLocalDescription \r\n%s",a_sdp.c_string());
  }
  ret = waitGetDescription(pc_sender, &RTCPeerConnection::GetLocalDescription, o_sdp, o_type, error);
  if (ret < 0) {
    printf("+++ pc_sender\tGetLocalDescription ERR:%s \r\n", error.c_string());
  } else {
    printf("+++ pc_sender\tGetLocalDescription \r\n");
    // printf("+++ pc_sender\tGetLocalDescription \r\n%s",o_sdp.c_string());
  }
  return ret;
}

RTCConfiguration config_;
int main() {
#ifdef WIN32
  WORD wVer;
  WSADATA wsaData;
  wVer = MAKEWORD(1, 1);
  auto err = WSAStartup(wVer, &wsaData);  //判断Windows sockets dll版本
  if (err != 0) {
    return (0);
  }
#endif  // WIN32

  LibWebRTC::Initialize();
  // config_.ice_servers[0].uri=
  // config_.ice_servers[0].username=
  // config_.ice_servers[0].password=
  config_.sdp_semantics = SdpSemantics::kUnifiedPlan;
  config_.tcp_candidate_policy = TcpCandidatePolicy::kTcpCandidatePolicyDisabled;
  config_.bundle_policy = BundlePolicy::kBundlePolicyMaxBundle;

  //初始化PC工厂
  scoped_refptr<RTCPeerConnectionFactory> pcFactory =
      LibWebRTC::CreateRTCPeerConnectionFactory();
  pcFactory->Initialize();
  //屏幕设备测试
  // auto screen_device_ = pcFactory->GetDesktopDevice();
  // auto video_screen_ = screen_device_->CreateScreenCapturer();
  // auto video_window_ = screen_device_->CreateWindowCapturer();

  //枚举视频设备
  auto video_device_ = pcFactory->GetVideoDevice();
  int cnum = video_device_->NumberOfDevices();
  printf(" Number Of Video Devices %d \r\n", cnum);
  char deviceNameUTF8[255];
  char deviceUniqueIdUTF8[255];
  for (int i = 0; i < cnum; i++) {
    video_device_->GetDeviceName(i, deviceNameUTF8, 254, deviceUniqueIdUTF8,
                                 254);
    printf(" Name Of Video Devices [%s] [%s]\r\n", deviceNameUTF8,
           deviceUniqueIdUTF8);
  }

  //创建摄像设备源
  auto video_caputer_ = video_device_->Create(deviceNameUTF8, 0, 640, 480, 30);
  auto constraints = RTCMediaConstraints::Create();
  auto video_source_ =
      pcFactory->CreateVideoSource(video_caputer_, "Test", constraints);
  auto video_track_ = pcFactory->CreateVideoTrack(video_source_, "Video_Test0");
  // video_track_->AddRenderer(new RTCVideoRendererImpl("Local Renderer"));

  //枚举音频设备
  auto audio_device_ = pcFactory->GetAudioDevice();
  int rnum = audio_device_->RecordingDevices();
  int pnum = audio_device_->PlayoutDevices();
  printf(" Number Of Audio Recording Devices %d \r\n", rnum);
  printf(" Number Of Audio Playout Devices %d \r\n", pnum);
  char name[RTCAudioDevice::kAdmMaxDeviceNameSize];
  char guid[RTCAudioDevice::kAdmMaxGuidSize];
  for (int i = 0; i < rnum; i++) {
    audio_device_->RecordingDeviceName(i, name, guid);
    printf(" Name Of Audio Recording Devices [%s] [%s] \r\n", name, guid);
  }
  for (int i = 0; i < pnum; i++) {
    audio_device_->PlayoutDeviceName(i, name, guid);
    printf(" Name Of Audio Playout Devices [%s] [%s] \r\n", name, guid);
  }
  audio_device_->SetPlayoutDevice(0);
  audio_device_->SetRecordingDevice(0);

  //创建音频设备源
  auto audio_source_ = pcFactory->CreateAudioSource("Test");
  auto audio_track_ = pcFactory->CreateAudioTrack(audio_source_, "Audio_Test");

  //创建PC
  auto pc_mc = RTCMediaConstraints::Create();
  pc_mc->AddMandatoryConstraint(RTCMediaConstraints::kEnableIPv6,
                                RTCMediaConstraints::kValueFalse);
  pc_mc->AddOptionalConstraint(RTCMediaConstraints::kEnableIPv6,
                               RTCMediaConstraints::kValueFalse);
  // pc_mc->AddMandatoryConstraint(RTCMediaConstraints::kIceRestart,"true");
  auto pc_offer = pcFactory->Create(config_, pc_mc);
  auto pc_answer = pcFactory->Create(config_, pc_mc);

  //创建DC
  RTCDataChannelInit* dcInit = new RTCDataChannelInit();
  auto dc = pc_offer->CreateDataChannel("api", dcInit);
  dc->RegisterObserver([](scoped_refptr<RTCDataChannel> dc) {
    class _ : public RTCDataChannelObserver {
     public:
      _(scoped_refptr<RTCDataChannel> dc)
          : dc_(dc) {}
      virtual void OnStateChange(RTCDataChannelState state) {
        std::cout << "Offer RTCDataChannel [" << dc_->label().c_string()
                  << "]OnStateChange:" << state << std::endl;
      }
      virtual void OnMessage(const char* buffer, int length, bool binary) {
        std::cout << "Offer RTCDataChannel[" << dc_->label().c_string()
                  << "]OnMessage [" << buffer << "] len:" << length
                  << std::endl;
      }

     private:
      scoped_refptr<RTCDataChannel> dc_;
    };
    return new _(dc);
  }(dc));

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
#if 0
  std::vector<std::string> stream_ids({"Test1"});
  libwebrtc::scoped_refptr<libwebrtc::RTCRtpEncodingParameters> encoding_l =
      RTCRtpEncodingParameters::Create();
  encoding_l->set_active(true);
  encoding_l->set_max_bitrate_bps(500000);
  encoding_l->set_min_bitrate_bps(100000);
  encoding_l->set_max_framerate(15);
  // encoding_l->set_num_temporal_layers(2);//同播数量
  encoding_l->set_rid("l");
  encoding_l->set_scale_resolution_down_by(2.0);  //数值越大尺寸越小
  libwebrtc::scoped_refptr<libwebrtc::RTCRtpEncodingParameters> encoding_h =
      RTCRtpEncodingParameters::Create();
  encoding_h->set_active(true);
  encoding_h->set_max_bitrate_bps(900000);
  encoding_h->set_min_bitrate_bps(100000);
  encoding_h->set_max_framerate(30);
  // encoding_h->set_num_temporal_layers(1);//同播数量
  encoding_h->set_rid("h");
  encoding_h->set_scale_resolution_down_by(1.0);  //数值越大尺寸越小
  std::vector<scoped_refptr<RTCRtpEncodingParameters>> encodings_l;
  encodings_l.push_back(encoding_l);
  auto init = RTCRtpTransceiverInit::Create(
      RTCRtpTransceiverDirection::kSendOnly, stream_ids, encodings_l);
  // init->set_direction(RTCRtpTransceiverDirection::kSendOnly);
  pc_offer->AddTransceiver(
      pcFactory->CreateVideoTrack(video_source_, "Video_Test1"), init);
  pc_offer->AddTransceiver(
      pcFactory->CreateAudioTrack(audio_source_, "Audio_Test1"));
  // pc_answer->AddTransceiver(pcFactory->CreateAudioTrack(audio_source_,
  // "Audio_Test1"));
  //  auto trans =
  //  pc_offer->AddTransceiver(pcFactory->CreateVideoTrack(video_source_,
  //  "Video_Test1")); pc_answer->AddTransceiver(RTCMediaType::VIDEO);
  //  pc_answer->AddTransceiver(RTCMediaType::VIDEO);
  //  pc_answer->AddTransceiver(RTCMediaType::AUDIO);
#endif
  // 3.使用AddTrack方式添加媒体
#if 1
  std::vector<std::string> stream_ids({"Test"});
  // pc_offer->AddTrack(pcFactory->CreateAudioTrack(audio_source_, "Audio_Test2"),stream_ids);
  // pc_offer->AddTrack(pcFactory->CreateAudioTrack(audio_source_, "Audio_Test3"),stream_ids);
  // pc_offer->AddTrack(pcFactory->CreateAudioTrack(audio_source_, "Audio_Test4"),stream_ids);
  pc_offer->AddTrack(pcFactory->CreateVideoTrack(video_source_, "Video_Test1"), stream_ids);

  // auto v_sender=pc_offer->AddTrack(pcFactory->CreateVideoTrack(video_source_, "Video_Test2"),stream_ids);
  // auto param=v_sender->parameters();
  // auto encodings=v_sender->init_send_encodings();
  // std::cout << "==> encodings size:" <<encodings.size() <<std::endl;
  // for (scoped_refptr<RTCRtpEncodingParameters> encoding : encodings.std_vector()) {
  //   std::cout << "==> min_bitrate_bps " <<encoding->min_bitrate_bps() <<std::endl;
  //   std::cout << "==> max_bitrate_bps " <<encoding->max_bitrate_bps() <<std::endl;
  //   std::cout << "==> max_framerate " <<encoding->max_framerate() <<std::endl;
  // }
  // pc_answer->AddTrack(pcFactory->CreateAudioTrack(audio_source_, "Audio_Test1"),stream_ids);
  // pc_answer->AddTrack(pcFactory->CreateVideoTrack(video_source_, "Video_Test1"),stream_ids);
#endif

  //  auto trackStats = scoped_refptr<TrackStatsObserverImpl>(new
  //  RefCountedObject<TrackStatsObserverImpl>());

  //设置状态回调
  pc_answer->RegisterRTCPeerConnectionObserver(
      new RTCPeerConnectionObserverImpl("answer", pc_answer, pc_offer));
  pc_offer->RegisterRTCPeerConnectionObserver(
      new RTCPeerConnectionObserverImpl("offer", pc_offer, pc_answer));

  //交换SDP
  exchangeDescription(pc_offer, pc_answer);

  //添加新的的视频Track
#if 0
  getchar();
  std::vector<std::string> stream_ids_1({"Test_2"});
  std::vector<scoped_refptr<RTCRtpEncodingParameters>> encodings_h;
  encodings_h.push_back(encoding_h);
  pc_offer->AddTransceiver(pcFactory->CreateVideoTrack(video_source_, "Video_Test2"),
                           RTCRtpTransceiverInit::Create(RTCRtpTransceiverDirection::kSendOnly, stream_ids_1, encodings_h));
#endif
  //重新协商ICE
  // for(int i=0;i<5;i++) usleep(1000000);
  // pc_offer->RestartIce();
  getchar();
  std::vector<std::string> stream_ids1({"Test1"});
  pc_offer->AddTrack(pcFactory->CreateAudioTrack(audio_source_, "Audio_Test1"), stream_ids1);
  getchar();
  //循环发送DC消息
  // do {
  printf("+++ Send ChannelData \r\n");
  string msg = "Hello World";
  dc->Send((const uint8_t*)msg.c_string(), msg.size() + 1, false);
  // usleep(1000000);
  // } while (true);

  getchar();
  pc_offer->Close();
  pc_answer->Close();
  printf("Fininsh\r\n");
  audio_device_ = nullptr;
  video_device_ = nullptr;
  pcFactory->Terminate();
}