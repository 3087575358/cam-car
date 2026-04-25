这里选用一个最基础的xclk值逻辑传输链路作为开端，好让大伙对物联网的基本模式有个概念
# 用户端
## 用户端的样式编写与交互编写(客户端-界面)
这一段的主要内容就是利用css定义的样式写的HTML页面
```html
<section id="xclk-section" class="nothidden">
    <div class="input-group" id="set-xclk-group">
        <label for="set-xclk">相机传感器基频</label>
        <div class="text">
            <label>XCLK</label>
            <input id="xclk" type="text" minlength="1" maxlength="2" size="2" value="20">
            <label>MHz</label>
        </div>
        <button class="inline-button" id="set-xclk">设置</button>
    </div>
</section>
```

在用户进行相应操作后
## 编写监听事件的JavaScript代码（客户端-后端逻辑）
这一段的主要内容就是利用JavaScript监听用户的操作，并将用户输入的数据发送到服务器端进行处理

```javascript
    document.addEventListener('DOMContentLoaded', function (event) {
    const setXclkButton = document.getElementById('set-xclk')
    // 监听设置相机传感器基频的按钮点击事件'set-xclk',对应语句
    // <button class="inline-button" id="set-xclk">设置</button>
    // 当set-xclk被点击后，会生成一个click事件，进而查找并触发onclick对应定义的函数
    setXclkButton.onclick = () => {
        let xclk = parseInt(document.getElementById('xclk').value);
        // 找到id为xclk的输入框，并获取用户输入的值，转换为整数类型
        setXclk(xclk, function(code, txt){
            if(code != 200){
                alert('Error['+code+']: '+txt);
            }
        });
    }
    })
```


发送给服务端的函数定义如下
```javascript
    function setXclk(xclk, cb){
        fetchUrl(`${baseHost}/xclk?xclk=${xclk}`, cb);
    }
</script>
```

注意fetchurl函数他没有使用库封装的而是自己定义了一个
```javascript
    function fetchUrl(url, cb){
        fetch(url)
        .then(function (response) {
            if (response.status !== 200) {
            cb(response.status, response.statusText);
            } 
            else {
            response.text()
            .then(function(data){
                cb(200, data);
            })
            .catch(function(err) {
                cb(-1, err);
            });
            }
        })
        .catch(function(err) {
            cb(-1, err);
        });
    }
```

上面那个写一堆其实运行起来网络硬件不出问题的话，就两句话
```javascript
\\删去网络异常处理部分就只剩下
function fetchUrl(url, cb){
        fetch(url).then().catch()}
```



与一般的GET-POST模式不同，这里是通过fetch函数直接向服务器发送一个GET请求，并在URL中携带参数xclk
其格式为```http://192.168.1.34/xclk?xclk=20```
服务器接收到这个请求后，会根据参数xclk的值进行相应的处理，并返回处理结果。

如果你搞大型项目千万别这么弄，当然咱这里的小项目不写那么多的回传就直接用GET了


# 服务器端
## 而esp32cam作为服务器端，根据协议自动接收到消息如下(服务器端-请求域的识别)

```h
GET /xclk?xclk=20 HTTP/1.1
Host: 192.168.1.34
```
**在httpd_handle_t camera_httpd = NULL;的服务器句柄正常启动之后**:
1. 在我的们的服务器启动函数中，通过httpd_register_uri_handler函数将这个URI处理函数注册到服务器中
```cpp
        if (httpd_start(&camera_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
        httpd_register_uri_handler(camera_httpd, &bmp_uri);

        httpd_register_uri_handler(camera_httpd, &xclk_uri);
        httpd_register_uri_handler(camera_httpd, &reg_uri);
        httpd_register_uri_handler(camera_httpd, &greg_uri);
        httpd_register_uri_handler(camera_httpd, &pll_uri);
        httpd_register_uri_handler(camera_httpd, &win_uri);
    }
```

2. 将特定的URI路径（例如 /xclk）与一个对应的处理函数（例如 xclk_handler）进行绑定。<br>
当有HTTP请求到达这个路径时，ESP-IDF的HTTP服务器库会自动调用您绑定的那个处理函数。
```cpp
httpd_uri_t xclk_uri = {
    .uri = "/xclk",             //uri主页面划分为/xclk、/reg、/greg、/pll、/resolution等不同的功能模块
    .method = HTTP_GET,
    .handler = xclk_handler,    //对应的处理函数为xclk_handler，参考1259行，负责处理设置晶振时钟的命令
    .user_ctx = NULL,

    #ifdef CONFIG_HTTPD_WS_SUPPORT
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
    #endif
    };
```

## esp32cam的硬件处理(服务器端-handler函数调用固件函数)
3. 处理函数做的事情就两件：
读取数据的长度并设置一个缓冲区来存储数据
根据array_to_int解析出参数xclk的值，并进行相应的处理设置工作频率set_xclk(xclk);
其他操作本质是对联网协议的维护和方便debug的处理，设置响应报头和响应内容等
```cpp
static esp_err_t xclk_handler(httpd_req_t *req)
// 注意这里的函数类型定义为esp_err_t，表示这个函数会返回一个ESP错误代码，通常是ESP_OK（0）表示成功，其他值表示不同类型的错误
{
    /*  
        通过设置req对应的报文格式如下
        "xclk=9600"
        设置晶振时钟
    */
    char *buf = NULL;
    char _xclk[32];

    if (parse_get(req, &buf) != ESP_OK) {
        return ESP_FAIL;
    }
    if (httpd_query_key_value(buf, "xclk", _xclk, sizeof(_xclk)) != ESP_OK) {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    free(buf);

    int xclk = atoi(_xclk);//转char*为int
    log_i("Set XCLK: %d MHz", xclk);//将行为记录日志

    sensor_t *s = esp_camera_sensor_get();
    int res = s->set_xclk(s, LEDC_TIMER_0, xclk);//timer设置成功，res返回为1
    
    /*
    #define ESP_OK          0       // 操作成功
    #define ESP_FAIL       -1       // 通用失败
    #define ESP_ERR_NO_MEM -2       // 内存不足
    #define ESP_ERR_INVALID_ARG -3  // 无效参数
    #define ESP_ERR_TIMEOUT    -4   // 操作超时
    #define ESP_ERR_NOT_FOUND  -5   // 未找到资源
    */
    // 更多错误代码在esp_err.h中定义
    // res in if will be swapped to a bull value, if res is 0, it will be false, otherwise it will be true
    // 一般的操作成功返回的会是正数，然后失败返回0或者负数，但是ESP_OK=0 ，所以不要奇怪...
    if (res) {
        return httpd_resp_send_500(req);
    }
    //只有报头，仅用于tcp协议再次确认握手链接，而没有报文内容
    //httpd_resp_set_type(req, "text/plain");//因此这句代码可以省略（默认格式内容无须注明并分配content）
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);//返回全域控制权与结束符
}
```
set_xclk作为一个固件函数，定义就不用多讲了，

唯一需要注意的是它的返回值esp_err_t的取值逻辑，不然容易看不懂。。。






















































