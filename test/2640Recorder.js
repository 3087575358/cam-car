/**
 * ESP32-CAM MJPEG 流 → MP4 录制器
 * 基于 WebCodecs API (Chrome/Edge 94+)
 * 内存占用：~20-50MB，支持长时间录制
 */

// 定义Recorder结构体, 用于存放录制状态和寻址指针
class ESP32MP4Recorder {
    constructor() {
        this.isRecording = false;
        this.frameCount = 0;
        this.startTime = 0;
        
        // WebCodecs 实例
        this.decoder = null;
        this.encoder = null;
        
        // MP4 封装
        this.mp4box = null;
        this.trackId = null;
        this.sampleNum = 0;
        
        // 流控制
        this.abortController = null;
        this.readableStream = null;
        
        // 编码参数缓存
        this.encoderConfig = null;
    }

    /**
     * 初始化录制器
     * @param {Object} options
     * @param {number} options.width - 输出宽度 (默认640)
     * @param {number} options.height - 输出高度 (默认480)
     * @param {number} options.fps - 目标帧率 (默认15，匹配ESP32)
     * @param {number} options.bitrate - 视频码率 bps (默认800000)
     * @param {string} options.streamUrl - ESP32 流地址
     */
    async init(options = {}) {
        const {
            width = 640,
            height = 480,
            fps = 15,
            bitrate = 800000,
            streamUrl = 'http://192.168.4.1:81/stream'
        } = options;

        this.config = { width, height, fps, bitrate, streamUrl };

        // 检查 WebCodecs 支持
        if (!('VideoDecoder' in window) || !('VideoEncoder' in window)) {
            throw new Error('WebCodecs API 不支持，请使用 Chrome/Edge 94+');
        }

        // 检查 MJPEG 解码支持
        const mjpegSupport = await VideoDecoder.isConfigSupported({
            codec: 'mjpeg',
            codedWidth: width,
            codedHeight: height
        });
        if (!mjpegSupport.supported) {
            throw new Error('浏览器不支持 MJPEG 硬件解码');
        }

        // 初始化 MP4Box.js (需要引入 https://github.com/gpac/mp4box.js)
        await this._initMP4Box();

        return this;
    }

    /**
     * 初始化 MP4 封装器
     */
    async _initMP4Box() {
        // 动态加载 MP4Box.js (如果你用模块化构建，可以直接 import)
        if (typeof MP4Box === 'undefined') {
            await this._loadScript('https://cdn.jsdelivr.net/npm/mp4box@0.5.3/dist/mp4box.all.min.js');
        }
        
        this.mp4box = MP4Box.createFile();
        this.mp4box.onReady = (info) => {
            console.log('MP4 封装就绪:', info);
        };
        this.mp4box.onError = (e) => {
            console.error('MP4 封装错误:', e);
        };
    }

    _loadScript(src) {
        return new Promise((resolve, reject) => {
            const script = document.createElement('script');
            script.src = src;
            script.onload = resolve;
            script.onerror = reject;
            document.head.appendChild(script);
        });
    }

    /**
     * 开始录制
     */
    async start() {
        if (this.isRecording) return;
        this.isRecording = true;
        this.frameCount = 0;
        this.startTime = performance.now();
        this.sampleNum = 0;

        const { width, height, fps, bitrate, streamUrl } = this.config;

        // 1. 初始化 MJPEG 解码器
        this.decoder = new VideoDecoder({
            output: (videoFrame) => this._onDecodedFrame(videoFrame),
            error: (e) => console.error('解码错误:', e)
        });

        await this.decoder.configure({
            codec: 'mjpeg',
            codedWidth: width,
            codedHeight: height
        });

        // 2. 初始化 H.264 编码器
        this.encoder = new VideoEncoder({
            output: (chunk, metadata) => this._onEncodedChunk(chunk, metadata),
            error: (e) => console.error('编码错误:', e)
        });

        this.encoderConfig = {
            codec: 'avc1.42001E', // H.264 Baseline profile, level 3.0
            width: width,
            height: height,
            bitrate: bitrate,
            framerate: fps,
            latencyMode: 'realtime', // 低延迟模式
            hardwareAcceleration: 'prefer-hardware' // 优先硬件加速
        };

        await this.encoder.configure(this.encoderConfig);

        // 3. 添加视频轨道到 MP4
        this.trackId = this.mp4box.addTrack({
            timescale: 1000, // 毫秒时间基
            width: width,
            height: height,
            nb_samples: 0,
            avcDecoderConfigRecord: null // 将在第一个 metadata 时填充
        });

        // 4. 开始拉取 MJPEG 流
        this.abortController = new AbortController();
        
        try {
            const response = await fetch(streamUrl, {
                signal: this.abortController.signal
            });
            
            this.readableStream = response.body;
            await this._parseMJPEGStream(this.readableStream);
            
        } catch (err) {
            if (err.name !== 'AbortError') {
                console.error('流获取失败:', err);
                this.stop();
            }
        }
    }

    /**
     * 解析 MJPEG 流 (multipart/x-mixed-replace)
     */
    async _parseMJPEGStream(readableStream) {
        const reader = readableStream.getReader();
        const boundary = '--123456789000000000000987654321'; // ESP32-CAM 默认 boundary
        
        let buffer = new Uint8Array(0);
        let isFirstFrame = true;

        try {
            while (this.isRecording) {
                const { done, value } = await reader.read();
                if (done) break;

                // 追加新数据到缓冲区
                const newBuffer = new Uint8Array(buffer.length + value.length);
                newBuffer.set(buffer);
                newBuffer.set(value, buffer.length);
                buffer = newBuffer;

                // 查找 JPEG 帧边界
                let frameStart = -1;
                let frameEnd = -1;

                // 查找 boundary 后的 Content-Type: image/jpeg
                const boundaryPattern = new TextEncoder().encode(boundary);
                
                // 简化的帧提取：查找 JPEG SOI (0xFFD8) 和 EOI (0xFFD9)
                for (let i = 0; i < buffer.length - 1; i++) {
                    if (buffer[i] === 0xFF && buffer[i + 1] === 0xD8) {
                        frameStart = i;
                    }
                    if (buffer[i] === 0xFF && buffer[i + 1] === 0xD9 && frameStart !== -1) {
                        frameEnd = i + 2;
                        break;
                    }
                }

                if (frameStart !== -1 && frameEnd !== -1 && frameEnd > frameStart) {
                    // 提取完整 JPEG 帧
                    const jpegFrame = buffer.slice(frameStart, frameEnd);
                    
                    // 剩余数据保留到下一次
                    buffer = buffer.slice(frameEnd);
                    
                    // 送入解码器
                    this._decodeJPEGFrame(jpegFrame, isFirstFrame);
                    isFirstFrame = false;
                }
            }
        } finally {
            reader.releaseLock();
        }
    }

    /**
     * 解码 JPEG 帧
     */
    _decodeJPEGFrame(jpegData, isKeyFrame = false) {
        const chunk = new EncodedVideoChunk({
            type: isKeyFrame ? 'key' : 'delta',
            timestamp: performance.now() - this.startTime, // 毫秒
            data: jpegData
        });

        this.decoder.decode(chunk);
        this.frameCount++;
    }

    /**
     * 解码完成回调：送入编码器
     */
    _onDecodedFrame(videoFrame) {
        // 直接编码，不缓存帧
        this.encoder.encode(videoFrame, {
            keyFrame: this.frameCount % 30 === 0 // 每30帧一个关键帧
        });
        
        videoFrame.close(); // 立即释放 GPU 内存
    }

    /**
     * 编码完成回调：封装进 MP4
     */
    _onEncodedChunk(chunk, metadata) {
        // 如果是第一帧，提取 decoder config (SPS/PPS)
        if (metadata && metadata.decoderConfig) {
            const config = metadata.decoderConfig;
            if (config.description) {
                // 设置 avcC box (H.264 配置)
                this.mp4box.setAVCDecoderConfigRecord(this.trackId, config.description);
            }
        }

        // 转换 chunk 为 MP4 sample
        const buffer = new ArrayBuffer(chunk.byteLength);
        chunk.copyTo(buffer);
        
        const sampleDuration = 1000 / this.config.fps; // 每帧时长(ms)

        this.mp4box.addSample(this.trackId, buffer, {
            duration: sampleDuration,
            dts: chunk.timestamp,
            cts: chunk.timestamp,
            is_sync: chunk.type === 'key'
        });

        this.sampleNum++;
    }

    /**
     * 停止录制并下载
     */
    async stop() {
        if (!this.isRecording) return;
        this.isRecording = false;

        // 中止 fetch
        this.abortController?.abort();

        // 刷新编解码器
        await this.decoder?.flush();
        await this.encoder?.flush();

        // 关闭编解码器
        this.decoder?.close();
        this.encoder?.close();

        // 完成 MP4 封装
        this.mp4box.save('esp32-recording.mp4');
        
        console.log(`录制完成: ${this.frameCount} 帧, ${this.sampleNum} samples`);
    }

    /**
     * 获取录制状态
     */
    getStatus() {
        if (!this.isRecording) return { recording: false };
        
        const duration = (performance.now() - this.startTime) / 1000;
        return {
            recording: true,
            frameCount: this.frameCount,
            duration: duration.toFixed(1),
            fps: (this.frameCount / duration).toFixed(1)
        };
    }
}

// ==================== 使用示例 ====================

let recorder = null;

async function startRecording() {
    try {
        recorder = new ESP32MP4Recorder();
        await recorder.init({
            streamUrl: 'http://192.168.4.1:81/stream',
            width: 640,
            height: 480,
            fps: 15,
            bitrate: 1000000
        });
        
        await recorder.start();
        updateStatus();
        
    } catch (err) {
        alert('启动失败: ' + err.message);
        console.error(err);
    }
}

function stopRecording() {
    recorder?.stop();
    updateStatus();
}

function updateStatus() {
    const status = recorder?.getStatus() || { recording: false };
    document.getElementById('status').textContent = 
        status.recording 
            ? `录制中: ${status.frameCount}帧, ${status.duration}s, ${status.fps}fps`
            : '未录制';
    
    if (status.recording) {
        requestAnimationFrame(updateStatus);
    }
}