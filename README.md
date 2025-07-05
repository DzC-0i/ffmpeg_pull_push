# ffmpeg_pull_push

推流前需要启动RTSP服务器，需要运行mediamtx(其他也可以)

访问: [mediamtx](https://github.com/bluenviron/mediamtx/releases) 下载

```bash
# 程序启动命令
./mediamtx --config config.yml

# 拉流RTSP推流RTMP
./video_streamer rtsp://192.168.13.151:554 rtmp rtmp://127.0.0.1:1935/stream
# 拉流RTSP推流RTSP
./video_streamer rtsp://192.168.13.151:554 rtsp rtsp://127.0.0.1:8554/stream
```
