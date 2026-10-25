#include "streaming/video/remotedecoder.h"
#include "streaming/video/encodedframe.h"
#include "streaming/canvasbridge.h"
#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>
#include <future>
#include <cassert>
#include <cstdio>
int main(int argc,char**argv) {
 QCoreApplication app(argc,argv); SDL_SetMainReady(); assert(SDL_Init(SDL_INIT_EVENTS)==0);
 QTemporaryDir tmp("/tmp/dp-video-XXXXXX"); assert(tmp.isValid());
 qputenv("DESKPORT_VIDEO_ENDPOINT",(tmp.path()+"/v").toUtf8()); qputenv("DESKPORT_SESSION_TOKEN","isolated-fixture");
 // Match production worker bootstrap: environment secrets are removed before
 // the decoder starts, and must also remain available for decoder recreation.
 CanvasBridge::setVideoTransport(qEnvironmentVariable("DESKPORT_VIDEO_ENDPOINT"), qEnvironmentVariable("DESKPORT_SESSION_TOKEN"));
 const auto endpoint = qEnvironmentVariable("DESKPORT_VIDEO_ENDPOINT");
 qunsetenv("DESKPORT_SESSION_TOKEN"); qunsetenv("DESKPORT_VIDEO_ENDPOINT");
 std::promise<void> listening, consumed;
 std::thread server([&]{
  QLocalServer s; assert(s.listen(endpoint)); listening.set_value();
  assert(s.waitForNewConnection(5000)); auto socket=s.nextPendingConnection(); assert(socket);
  QByteArray buffer;
  auto more=[&]{ assert(socket->bytesAvailable() || socket->waitForReadyRead(5000)); buffer+=socket->readAll(); };
  while(!buffer.contains('\n')) more(); int e=buffer.indexOf('\n');
  assert(QJsonDocument::fromJson(buffer.left(e)).object()["token"]=="isolated-fixture"); buffer.remove(0,e+1);
  auto packet=[&]{ while(buffer.size()<4)more(); auto size=qFromBigEndian<quint32>(buffer.constData()); assert(size<1024*1024); while(buffer.size()<int(size+4))more(); auto p=buffer.mid(4,size); buffer.remove(0,size+4); return p; };
  auto config=QJsonDocument::fromJson(packet()).object(); assert(config["width"]==1280 && config["height"]==720);
  socket->write("{\"ok\":true,\"hardware\":true,\"hdr\":false,\"colorspace\":1,\"range\":0}\n"); assert(socket->waitForBytesWritten(5000));
  auto frame=EncodedVideoFrame::decode(packet(),100); assert(frame && frame->generation==quint64(config["generation"].toDouble()));
  assert(frame->unit.fullLength==5 && frame->unit.frameNumber==42 && frame->unit.frameType==FRAME_TYPE_IDR);
  socket->abort(); consumed.set_value();
 });
 listening.get_future().wait();
 {
  RemoteVideoDecoder decoder; DECODER_PARAMETERS params{}; params.width=1280;params.height=720;params.frameRate=60;params.videoFormat=VIDEO_FORMAT_H264;
  assert(decoder.initialize(&params)); assert(decoder.isHardwareAccelerated()); assert(!decoder.isHdrSupported());
  char bytes[]={0,0,1,0x65,0}; LENTRY segment{nullptr,bytes,5,BUFFER_TYPE_PICDATA}; DECODE_UNIT unit{};unit.fullLength=5;unit.bufferList=&segment;unit.frameType=FRAME_TYPE_IDR;unit.frameNumber=42;
  assert(decoder.submitDecodeUnit(&unit)==DR_OK); consumed.get_future().wait(); server.join();
  assert(decoder.submitDecodeUnit(&unit)==DR_OK);
  bool reset=false; for(int i=0;i<100&&!reset;++i){ SDL_Event event; while(SDL_PollEvent(&event)) if(event.type==SDL_RENDER_DEVICE_RESET)reset=true; SDL_Delay(10); }
  assert(reset);
 }
 CanvasBridge::setVideoTransport({}, {});
 { RemoteVideoDecoder decoder; DECODER_PARAMETERS params{}; assert(!decoder.initialize(&params)); }
 SDL_Quit(); puts("PASS: real encoded transport authentication after environment cleanup, missing credential rejection, configuration, frame integrity, peer-loss reset and bounded teardown");
}
