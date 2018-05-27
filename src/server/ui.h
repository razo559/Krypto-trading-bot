#ifndef K_UI_H_
#define K_UI_H_

extern const char _www_html_index,     _www_ico_favicon,     _www_css_base,
                  _www_gzip_bomb,      _www_mp3_audio_0,     _www_css_light,
                  _www_js_bundle,      _www_mp3_audio_1,     _www_css_dark;
extern const  int _www_html_index_len, _www_ico_favicon_len, _www_css_base_len,
                  _www_gzip_bomb_len,  _www_mp3_audio_0_len, _www_css_light_len,
                  _www_js_bundle_len,  _www_mp3_audio_1_len, _www_css_dark_len;
namespace K {
  class UI: public Klass,
            public Client { public: UI() { client = this; }
    private:
      int connections = 0;
      string B64auth = "",
             notepad = "";
      map<char, function<void(json*)>*> hello;
      map<char, function<void(const json&)>*> kisses;
      map<mMatter, string> queue;
    protected:
      void load() {
        if (args.headless
          or args.user.empty()
          or args.pass.empty()
        ) return;
        B64auth = string("Basic ") + FN::oB64(args.user + ':' + args.pass);
      };
      void waitData() {
        if (args.headless) return;
        auto client = &events->listen()->getDefaultGroup<uWS::SERVER>();
        client->onConnection([&](uWS::WebSocket<uWS::SERVER> *webSocket, uWS::HttpRequest req) {
          onConnection();
          const string addr = cleanAddress(webSocket->getAddress().address);
          screen->logUIsess(connections, addr);
          if (connections > args.maxAdmins) {
            screen->log("UI", "--client-limit=" + to_string(args.maxAdmins) + " reached by", addr);
            webSocket->close();
          }
        });
        client->onDisconnection([&](uWS::WebSocket<uWS::SERVER> *webSocket, int code, char *message, size_t length) {
          onDisconnection();
          screen->logUIsess(connections, cleanAddress(webSocket->getAddress().address));
        });
        client->onHttpRequest([&](uWS::HttpResponse *res, uWS::HttpRequest req, char *data, size_t length, size_t remainingBytes) {
          const string response = onHttpRequest(
            req.getUrl().toString(),
            req.getMethod() == uWS::HttpMethod::METHOD_GET,
            req.getHeader("authorization").toString(),
            cleanAddress(res->getHttpSocket()->getAddress().address)
          );
          if (!response.empty()) res->write(response.data(), response.length());
        });
        client->onMessage([&](uWS::WebSocket<uWS::SERVER> *webSocket, const char *message, size_t length, uWS::OpCode opCode) {
          if (length < 2) return;
          const string response = onMessage(
            string(message, length),
            !args.whitelist.empty()
              ? cleanAddress(webSocket->getAddress().address)
              : "unknown"
          );
          if (!response.empty())
            webSocket->send(
              response.data(),
              response.substr(0, 2) == "PK"
                ? uWS::OpCode::BINARY
                : uWS::OpCode::TEXT
            );
        });
        broadcast = [this, client](const mMatter &type, string msg) {
          msg.insert(msg.begin(), (char)type);
          msg.insert(msg.begin(), (char)mPortal::Kiss);
          events->deferred([client, msg]() {
            client->broadcast(msg.data(), msg.length(), uWS::OpCode::TEXT);
          });
        };
      };
      void waitUser() {
        if (args.headless) return;
        welcome = [&](const mMatter &type, function<void(json*)> *fn) {
          if (hello.find((char)type) != hello.end())
            exit(screen->error("UI", string("Use only a single unique message handler for \"")
              + (char)type + "\" welcome event"));
          hello[(char)type] = fn;
        };
        clickme = [&](const mMatter &type, function<void(const json&)> *fn) {
          if (kisses.find((char)type) != kisses.end())
            exit(screen->error("UI", string("Use only a single unique message handler for \"")
              + (char)type + "\" clickme event"));
          kisses[(char)type] = fn;
        };
        welcome(mMatter::ApplicationState, &helloServer);
        welcome(mMatter::ProductAdvertisement, &helloProduct);
        welcome(mMatter::Notepad, &helloNotes);
        clickme(mMatter::Notepad, &kissNotes);
      };
      void run() {
        send = send_nowhere;
      };
    public:
      void timer_Xs() {
        for (map<mMatter, string>::value_type &it : queue)
          broadcast(it.first, it.second);
        queue.clear();
      };
      void timer_60s() {
        send(mMatter::ApplicationState, serverState());
        engine->orders_60s = 0;
      };
    private:
      function<void(json*)> helloServer = [&](json *welcome) {
        *welcome = { serverState() };
      };
      function<void(json*)> helloProduct = [&](json *welcome) {
        *welcome = { {
          {"exchange", gw->exchange},
          {"pair", mPair(gw->base, gw->quote)},
          {"minTick", gw->minTick},
          {"environment", args.title},
          {"matryoshka", args.matryoshka},
          {"homepage", "https://github.com/ctubio/Krypto-trading-bot"}
        } };
      };
      function<void(json*)> helloNotes = [&](json *welcome) {
        *welcome = { notepad };
      };
      function<void(const json&)> kissNotes = [&](const json &butterfly) {
        if (butterfly.is_array() and butterfly.size())
          notepad = butterfly.at(0);
      };
      function<void(const mMatter&, string)> broadcast = [](const mMatter &type, string msg) {};
      function<void(const mMatter&, const json&)> send_nowhere = [](const mMatter &type, const json &msg) {};
      function<void(const mMatter&, const json&)> send_somewhere = [&](const mMatter &type, const json &msg) {
        if (!qp.delayUI or !delayed(type))
          broadcast(type, msg.dump());
        else queue[type] = msg.dump();
      };
      inline static bool delayed(const mMatter &type) {
        return type == mMatter::FairValue
          or type == mMatter::OrderStatusReports
          or type == mMatter::QuoteStatus
          or type == mMatter::Position
          or type == mMatter::TargetBasePosition
          or type == mMatter::EWMAChart;
      };
      inline void onConnection() {
        if (!connections++) send = send_somewhere;
      };
      inline void onDisconnection() {
        if (!--connections) send = send_nowhere;
      };
      inline string onHttpRequest(const string &path, const bool &get, const string &auth, const string &addr) {
        string document,
               content;
        if (addr != "unknown" and !args.whitelist.empty() and args.whitelist.find(addr) == string::npos) {
          screen->log("UI", "dropping gzip bomb on", addr);
          document = "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nAccept-Ranges: bytes\r\nVary: Accept-Encoding\r\nCache-Control: public, max-age=0\r\nContent-Encoding: gzip\r\n";
          content = string(&_www_gzip_bomb, _www_gzip_bomb_len);
        } else if (!B64auth.empty() and auth.empty()) {
          screen->log("UI", "authorization attempt from", addr);
          document = "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"Basic Authorization\"\r\nConnection: keep-alive\r\nAccept-Ranges: bytes\r\nVary: Accept-Encoding\r\nContent-Type:text/plain; charset=UTF-8\r\n";
        } else if (!B64auth.empty() and auth != B64auth) {
          screen->log("UI", "authorization failed from", addr);
          document = "HTTP/1.1 403 Forbidden\r\nConnection: keep-alive\r\nAccept-Ranges: bytes\r\nVary: Accept-Encoding\r\nContent-Type:text/plain; charset=UTF-8\r\n";
        } else if (get) {
          document = "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nAccept-Ranges: bytes\r\nVary: Accept-Encoding\r\nCache-Control: public, max-age=0\r\n";
          const string leaf = path.substr(path.find_last_of('.')+1);
          if (leaf == "/") {
            document += "Content-Type: text/html; charset=UTF-8\r\n";
            if (connections < args.maxAdmins) {
              screen->log("UI", "authorization success from", addr);
              content = string(&_www_html_index, _www_html_index_len);
            } else {
              screen->log("UI", "--client-limit=" + to_string(args.maxAdmins) + " reached by", addr);
              content = "Thank you! but our princess is already in this castle!<br/>Refresh the page anytime to retry.";
            }
          } else if (leaf == "js") {
            document += "Content-Type: application/javascript; charset=UTF-8\r\nContent-Encoding: gzip\r\n";
            content = string(&_www_js_bundle, _www_js_bundle_len);
          } else if (leaf == "css") {
            document += "Content-Type: text/css; charset=UTF-8\r\n";
            if (path.find("css/bootstrap.min.css") != string::npos)
              content = string(&_www_css_base, _www_css_base_len);
            else if (path.find("css/bootstrap-theme-dark.min.css") != string::npos)
              content = string(&_www_css_dark, _www_css_dark_len);
            else if (path.find("css/bootstrap-theme.min.css") != string::npos)
              content = string(&_www_css_light, _www_css_light_len);
          } else if (leaf == "ico") {
            document += "Content-Type: image/x-icon\r\n";
            content = string(&_www_ico_favicon, _www_ico_favicon_len);
          } else if (leaf == "mp3") {
            document += "Content-Type: audio/mpeg\r\n";
            if (path.find("audio/0.mp3") != string::npos)
              content = string(&_www_mp3_audio_0, _www_mp3_audio_0_len);
            else if (path.find("audio/1.mp3") != string::npos)
              content = string(&_www_mp3_audio_1, _www_mp3_audio_1_len);
          }
          if (content.empty())
            if (FN::int64() % 21) {
              document = "HTTP/1.1 404 Not Found\r\n";
              content = "Today, is a beautiful day.";
            } else { // Humans! go to any random url to check your luck
              document = "HTTP/1.1 418 I'm a teapot\r\n";
              content = "Today, is your lucky day!";
            }
        }
        if (!document.empty())
          document += "Content-Length: " + to_string(content.length()) + "\r\n\r\n" + content;
        return document;
      };
      inline string onMessage(const string &message, const string &addr) {
        if (addr != "unknown" and args.whitelist.find(addr) == string::npos)
          return string(&_www_gzip_bomb, _www_gzip_bomb_len);
        if (mPortal::Hello == (mPortal)message[0] and hello.find(message[1]) != hello.end()) {
          json reply;
          (*hello[message[1]])(&reply);
          if (!reply.is_null())
            return message.substr(0, 2) + reply.dump();
        } else if (mPortal::Kiss == (mPortal)message[0] and kisses.find(message[1]) != kisses.end()) {
          json butterfly = json::parse(
            (message.length() > 2 and message[2] == '{')
              ? message.substr(2)
              : "{}"
          );
          for (json::iterator it = butterfly.begin(); it != butterfly.end();)
            if (it.value().is_null()) it = butterfly.erase(it); else it++;
          (*kisses[message[1]])(butterfly);
        }
        return "";
      };
      static string cleanAddress(string addr) {
        if (addr.length() > 7 and addr.substr(0, 7) == "::ffff:") addr = addr.substr(7);
        if (addr.length() < 7) addr.clear();
        return addr.empty() ? "unknown" : addr;
      };
      inline unsigned int memorySize() {
        string ps = FN::output(string("ps -p") + to_string(::getpid()) + " -orss | tail -n1");
        ps.erase(remove(ps.begin(), ps.end(), ' '), ps.end());
        if (ps.empty()) ps = "0";
        return stoi(ps) * 1e+3;
      };
      json serverState() {
        return {
          {"memory", memorySize()},
          {"freq", engine->orders_60s},
          {"theme", args.ignoreMoon + args.ignoreSun},
          {"dbsize", sqlite->size()},
          {"a", gw->A()}
        };
      };
  };
}

#endif
