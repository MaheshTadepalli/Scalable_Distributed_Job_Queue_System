#include "http_server.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket invalidSocket = INVALID_SOCKET;
void closeSocket(Socket socket) { closesocket(socket); }
#else
using Socket = int;
constexpr Socket invalidSocket = -1;
void closeSocket(Socket socket) { close(socket); }
#endif
}

HttpServer::HttpServer(JobQueue& queue, std::uint16_t port) : queue_(queue), port_(port) {}

int HttpServer::run() {
#if defined(_WIN32)
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        std::cerr << "Unable to start networking\n";
        return 1;
    }
#endif

    Socket server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == invalidSocket) {
        std::cerr << "Unable to create server socket\n";
        return 1;
    }

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "Unable to bind to port " << port_ << "\n";
        closeSocket(server);
        return 1;
    }

    if (listen(server, 64) < 0) {
        std::cerr << "Unable to listen on port " << port_ << "\n";
        closeSocket(server);
        return 1;
    }

    std::cout << "Job queue dashboard listening on http://localhost:" << port_ << "\n";

    while (true) {
        Socket client = accept(server, nullptr, nullptr);
        if (client == invalidSocket) {
            continue;
        }

        std::thread([this, client] {
            std::string request;
            char buffer[8192];
            const int received = recv(client, buffer, sizeof(buffer) - 1, 0);
            if (received > 0) {
                buffer[received] = '\0';
                request.assign(buffer);
                const auto lengthText = headerValue(request, "Content-Length");
                const auto expected = lengthText.empty() ? 0 : std::stoi(lengthText);
                while (static_cast<int>(bodyOf(request).size()) < expected) {
                    const int more = recv(client, buffer, sizeof(buffer) - 1, 0);
                    if (more <= 0) {
                        break;
                    }
                    buffer[more] = '\0';
                    request.append(buffer);
                }
            }

            const auto reply = handleRequest(request);
            send(client, reply.c_str(), static_cast<int>(reply.size()), 0);
            closeSocket(client);
        }).detach();
    }
}

std::string HttpServer::handleRequest(const std::string& request) {
    if (request.rfind("GET / ", 0) == 0 || request.rfind("GET /dashboard", 0) == 0) {
        return response("200 OK", "text/html; charset=utf-8", dashboard());
    }
    if (request.rfind("GET /health", 0) == 0) {
        return response("200 OK", "application/json", "{\"status\":\"ok\"}");
    }
    if (request.rfind("GET /metrics", 0) == 0) {
        return response("200 OK", "application/json", metricsJson());
    }
    if (request.rfind("GET /dead-letter", 0) == 0) {
        return response("200 OK", "application/json", jobsJson(true));
    }
    if (request.rfind("GET /jobs", 0) == 0) {
        return response("200 OK", "application/json", jobsJson(false));
    }
    if (request.rfind("POST /jobs", 0) == 0) {
        return response("202 Accepted", "application/json", createJob(request));
    }
    return response("404 Not Found", "application/json", "{\"error\":\"not found\"}");
}

std::string HttpServer::dashboard() const {
    return R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Distributed Job Queue</title>
  <style>
    :root { color-scheme: light; --ink:#1d2433; --muted:#647084; --line:#d8dee8; --bg:#f6f7f9; --panel:#fff; --green:#217a50; --red:#b42318; --amber:#a15c07; --blue:#2457c5; }
    * { box-sizing: border-box; }
    body { margin:0; font-family: Inter, Segoe UI, Arial, sans-serif; background:var(--bg); color:var(--ink); }
    header { padding:22px 28px; border-bottom:1px solid var(--line); background:var(--panel); display:flex; align-items:center; justify-content:space-between; gap:16px; }
    h1 { margin:0; font-size:22px; letter-spacing:0; }
    main { padding:24px 28px; max-width:1180px; margin:auto; }
    .grid { display:grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap:12px; }
    .metric, .panel { background:var(--panel); border:1px solid var(--line); border-radius:8px; }
    .metric { padding:16px; }
    .label { color:var(--muted); font-size:13px; }
    .value { font-size:30px; font-weight:750; margin-top:6px; }
    .row { display:flex; align-items:center; justify-content:space-between; gap:12px; }
    .panel { margin-top:18px; overflow:hidden; }
    .panel h2 { font-size:16px; margin:0; padding:16px; border-bottom:1px solid var(--line); }
    form { display:grid; grid-template-columns: 1fr 2fr 160px 120px; gap:10px; padding:16px; border-bottom:1px solid var(--line); }
    input, button { height:38px; border-radius:6px; border:1px solid var(--line); padding:0 10px; font:inherit; }
    button { background:var(--blue); color:white; border-color:var(--blue); cursor:pointer; font-weight:650; }
    table { width:100%; border-collapse:collapse; font-size:14px; }
    th, td { text-align:left; padding:11px 14px; border-bottom:1px solid var(--line); vertical-align:top; }
    th { color:var(--muted); font-size:12px; text-transform:uppercase; background:#fafbfc; }
    .status { font-weight:700; }
    .Completed { color:var(--green); } .DeadLettered { color:var(--red); } .Retrying { color:var(--amber); } .Running { color:var(--blue); }
    @media (max-width: 760px) { header, main { padding-left:14px; padding-right:14px; } .grid { grid-template-columns:repeat(2, 1fr); } form { grid-template-columns:1fr; } th:nth-child(3), td:nth-child(3) { display:none; } }
  </style>
</head>
<body>
  <header>
    <h1>Scalable Distributed Job Queue System</h1>
    <span class="label" id="updated">Updating...</span>
  </header>
  <main>
    <section class="grid" id="metrics"></section>
    <section class="panel">
      <h2>Submit Job</h2>
      <form id="jobForm">
        <input name="type" placeholder="Type, e.g. email" value="email">
        <input name="payload" placeholder="Payload" value="send welcome email">
        <input name="failuresBeforeSuccess" type="number" min="0" value="1" title="Failures before success">
        <button type="submit">Enqueue</button>
      </form>
      <h2>Jobs</h2>
      <table>
        <thead><tr><th>ID</th><th>Status</th><th>Type</th><th>Attempts</th><th>Arrival Time</th><th>Last Error</th></tr></thead>
        <tbody id="jobs"></tbody>
      </table>
    </section>
  </main>
  <script>
    const metricNames = ["submitted","queued","running","completed","failedAttempts","retriesScheduled","deadLettered","workers"];
    function title(name) { return name.replace(/([A-Z])/g, " $1").replace(/^./, c => c.toUpperCase()); }
    async function refresh() {
      const [metrics, jobs] = await Promise.all([fetch("/metrics").then(r => r.json()), fetch("/jobs").then(r => r.json())]);
      document.getElementById("metrics").innerHTML = metricNames.map(name => `<div class="metric"><div class="label">${title(name)}</div><div class="value">${metrics[name]}</div></div>`).join("");
      document.getElementById("jobs").innerHTML = jobs.slice(-30).reverse().map(job => `<tr><td>${job.id}</td><td class="status ${job.status}">${job.status}</td><td>${job.type}</td><td>${job.attempts}/${job.maxRetries + 1}</td><td>${job.arrivalTime}</td><td>${job.lastError || ""}</td></tr>`).join("");
      document.getElementById("updated").textContent = "Updated " + new Date().toLocaleTimeString();
    }
    document.getElementById("jobForm").addEventListener("submit", async event => {
      event.preventDefault();
      const data = Object.fromEntries(new FormData(event.target).entries());
      data.failuresBeforeSuccess = Number(data.failuresBeforeSuccess);
      await fetch("/jobs", { method:"POST", headers:{ "Content-Type":"application/json" }, body:JSON.stringify(data) });
      refresh();
    });
    refresh();
    setInterval(refresh, 1000);
  </script>
</body>
</html>)HTML";
}

std::string HttpServer::metricsJson() const {
    const auto m = queue_.metrics();
    std::ostringstream out;
    out << "{\"submitted\":" << m.submitted
        << ",\"completed\":" << m.completed
        << ",\"failedAttempts\":" << m.failedAttempts
        << ",\"retriesScheduled\":" << m.retriesScheduled
        << ",\"deadLettered\":" << m.deadLettered
        << ",\"queued\":" << m.queued
        << ",\"running\":" << m.running
        << ",\"workers\":" << m.workers << "}";
    return out.str();
}

std::string HttpServer::jobsJson(bool deadLetterOnly) const {
    const auto jobs = deadLetterOnly ? queue_.deadLetterJobs() : queue_.jobs();
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        const auto& job = jobs[i];
        if (i > 0) {
            out << ",";
        }
        out << "{\"id\":" << job.id
            << ",\"type\":\"" << escapeJson(job.type)
            << "\",\"payload\":\"" << escapeJson(job.payload)
            << "\",\"attempts\":" << job.attempts
            << ",\"maxRetries\":" << job.maxRetries
            << ",\"status\":\"" << statusName(job.status)
            << "\",\"arrivalTime\":\"" << job.arrivalTime
            << "\",\"lastError\":\"" << escapeJson(job.lastError) << "\"}";
    }
    out << "]";
    return out.str();
}

std::string HttpServer::createJob(const std::string& request) {
    const auto body = bodyOf(request);
    const auto type = jsonValue(body, "type", "default");
    const auto payload = jsonValue(body, "payload", "");
    const auto failures = jsonIntValue(body, "failuresBeforeSuccess", 0);
    const auto job = queue_.submit(type, payload, failures);
    std::ostringstream out;
    out << "{\"id\":" << job.id << ",\"status\":\"queued\",\"arrivalTime\":\"" << job.arrivalTime << "\"}";
    return out.str();
}

std::string HttpServer::response(std::string status, std::string contentType, std::string body) {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "Access-Control-Allow-Origin: *\r\n\r\n"
        << body;
    return out.str();
}

std::string HttpServer::headerValue(const std::string& request, const std::string& name) {
    auto lowered = request;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto key = name + ":";
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto pos = lowered.find(key);
    if (pos == std::string::npos) {
        return "";
    }
    const auto valueStart = pos + key.size();
    const auto valueEnd = request.find("\r\n", valueStart);
    auto value = request.substr(valueStart, valueEnd - valueStart);
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);
    return value;
}

std::string HttpServer::bodyOf(const std::string& request) {
    const auto pos = request.find("\r\n\r\n");
    return pos == std::string::npos ? "" : request.substr(pos + 4);
}

std::string HttpServer::jsonValue(const std::string& json, const std::string& key, const std::string& fallback) {
    const auto marker = "\"" + key + "\"";
    const auto keyPos = json.find(marker);
    if (keyPos == std::string::npos) {
        return fallback;
    }
    const auto colon = json.find(':', keyPos + marker.size());
    const auto quote = json.find('"', colon);
    if (colon == std::string::npos || quote == std::string::npos) {
        return fallback;
    }
    const auto end = json.find('"', quote + 1);
    if (end == std::string::npos) {
        return fallback;
    }
    return json.substr(quote + 1, end - quote - 1);
}

int HttpServer::jsonIntValue(const std::string& json, const std::string& key, int fallback) {
    const auto marker = "\"" + key + "\"";
    const auto keyPos = json.find(marker);
    if (keyPos == std::string::npos) {
        return fallback;
    }
    const auto colon = json.find(':', keyPos + marker.size());
    if (colon == std::string::npos) {
        return fallback;
    }
    const auto start = json.find_first_of("-0123456789", colon + 1);
    if (start == std::string::npos) {
        return fallback;
    }
    const auto end = json.find_first_not_of("-0123456789", start);
    return std::stoi(json.substr(start, end - start));
}

std::string HttpServer::escapeJson(const std::string& value) {
    std::string escaped;
    for (char c : value) {
        if (c == '"' || c == '\\') {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    return escaped;
}

std::string HttpServer::statusName(JobStatus status) {
    switch (status) {
        case JobStatus::Queued: return "Queued";
        case JobStatus::Running: return "Running";
        case JobStatus::Completed: return "Completed";
        case JobStatus::Retrying: return "Retrying";
        case JobStatus::DeadLettered: return "DeadLettered";
    }
    return "Unknown";
}
