// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License

#ifndef __PROCESS_HTTP_HPP__
#define __PROCESS_HTTP_HPP__

#include <ctype.h>
#include <stdint.h>

#include <atomic>
#include <initializer_list>
#include <iosfwd>
#include <memory>
#include <queue>
#include <sstream>
#include <string>
#include <vector>

#include <boost/functional/hash.hpp>

#include <process/address.hpp>
#include <process/future.hpp>
#include <process/owned.hpp>
#include <process/pid.hpp>

#include <stout/error.hpp>
#include <stout/hashmap.hpp>
#include <stout/ip.hpp>
#include <stout/json.hpp>
#include <stout/none.hpp>
#include <stout/nothing.hpp>
#include <stout/option.hpp>
#include <stout/stringify.hpp>
#include <stout/strings.hpp>
#include <stout/try.hpp>

namespace process {

// Forward declaration to break cyclic dependency.
template <typename T>
class Future;

namespace network {
class Socket;
} // namespace network {

namespace http {

// Status code reason strings, from the HTTP1.1 RFC:
// http://www.w3.org/Protocols/rfc2616/rfc2616-sec6.html
extern hashmap<uint16_t, std::string>* statuses;

// Represents a Uniform Resource Locator:
//   scheme://domain|ip:port/path?query#fragment
//
// This is actually a URI-reference (see 4.1 of RFC 3986).
//
// TODO(bmahler): The default port should depend on the scheme!
struct URL
{
  URL() = default;

  URL(const std::string& _scheme,
      const std::string& _domain,
      const uint16_t _port = 80,
      const std::string& _path = "/",
      const hashmap<std::string, std::string>& _query =
        (hashmap<std::string, std::string>()),
      const Option<std::string>& _fragment = None())
    : scheme(_scheme),
      domain(_domain),
      port(_port),
      path(_path),
      query(_query),
      fragment(_fragment) {}

  URL(const std::string& _scheme,
      const net::IP& _ip,
      const uint16_t _port = 80,
      const std::string& _path = "/",
      const hashmap<std::string, std::string>& _query =
        (hashmap<std::string, std::string>()),
      const Option<std::string>& _fragment = None())
    : scheme(_scheme),
      ip(_ip),
      port(_port),
      path(_path),
      query(_query),
      fragment(_fragment) {}

  Option<std::string> scheme;

  // TODO(benh): Consider using unrestricted union for 'domain' and 'ip'.
  Option<std::string> domain;
  Option<net::IP> ip;
  Option<uint16_t> port;
  std::string path;
  hashmap<std::string, std::string> query;
  Option<std::string> fragment;
};


std::ostream& operator<<(std::ostream& stream, const URL& url);


struct CaseInsensitiveHash
{
  size_t operator()(const std::string& key) const
  {
    size_t seed = 0;
    foreach (char c, key) {
      boost::hash_combine(seed, ::tolower(c));
    }
    return seed;
  }
};


struct CaseInsensitiveEqual
{
  bool operator()(const std::string& left, const std::string& right) const
  {
    if (left.size() != right.size()) {
      return false;
    }
    for (size_t i = 0; i < left.size(); ++i) {
      if (::tolower(left[i]) != ::tolower(right[i])) {
        return false;
      }
    }
    return true;
  }
};


struct Status
{
  static const uint16_t CONTINUE;
  static const uint16_t SWITCHING_PROTOCOLS;
  static const uint16_t OK;
  static const uint16_t CREATED;
  static const uint16_t ACCEPTED;
  static const uint16_t NON_AUTHORITATIVE_INFORMATION;
  static const uint16_t NO_CONTENT;
  static const uint16_t RESET_CONTENT;
  static const uint16_t PARTIAL_CONTENT;
  static const uint16_t MULTIPLE_CHOICES;
  static const uint16_t MOVED_PERMANENTLY;
  static const uint16_t FOUND;
  static const uint16_t SEE_OTHER;
  static const uint16_t NOT_MODIFIED;
  static const uint16_t USE_PROXY;
  static const uint16_t TEMPORARY_REDIRECT;
  static const uint16_t BAD_REQUEST;
  static const uint16_t UNAUTHORIZED;
  static const uint16_t PAYMENT_REQUIRED;
  static const uint16_t FORBIDDEN;
  static const uint16_t NOT_FOUND;
  static const uint16_t METHOD_NOT_ALLOWED;
  static const uint16_t NOT_ACCEPTABLE;
  static const uint16_t PROXY_AUTHENTICATION_REQUIRED;
  static const uint16_t REQUEST_TIMEOUT;
  static const uint16_t CONFLICT;
  static const uint16_t GONE;
  static const uint16_t LENGTH_REQUIRED;
  static const uint16_t PRECONDITION_FAILED;
  static const uint16_t REQUEST_ENTITY_TOO_LARGE;
  static const uint16_t REQUEST_URI_TOO_LARGE;
  static const uint16_t UNSUPPORTED_MEDIA_TYPE;
  static const uint16_t REQUESTED_RANGE_NOT_SATISFIABLE;
  static const uint16_t EXPECTATION_FAILED;
  static const uint16_t INTERNAL_SERVER_ERROR;
  static const uint16_t NOT_IMPLEMENTED;
  static const uint16_t BAD_GATEWAY;
  static const uint16_t SERVICE_UNAVAILABLE;
  static const uint16_t GATEWAY_TIMEOUT;
  static const uint16_t HTTP_VERSION_NOT_SUPPORTED;

  static std::string string(uint16_t code);
};


typedef hashmap<std::string,
                std::string,
                CaseInsensitiveHash,
                CaseInsensitiveEqual> Headers;


struct Request
{
  std::string method;

  // TODO(benh): Add major/minor version.

  // For client requests, the URL should be a URI.
  // For server requests, the URL may be a URI or a relative reference.
  URL url;

  Headers headers;

  // TODO(bmahler): Add a 'query' field which contains both
  // the URL query and the parsed form data from the body.

  std::string body;

  // TODO(bmahler): Ensure this is consistent with the 'Connection'
  // header; perhaps make this a function that checks the header.
  bool keepAlive;

  // For server requests, this contains the address of the client.
  // Note that this may correspond to a proxy or load balancer address.
  network::Address client;

  /**
   * Returns whether the encoding is considered acceptable in the
   * response. See RFC 2616 section 14.3 for details.
   */
  bool acceptsEncoding(const std::string& encoding) const;

  /**
   * Returns whether the media type is considered acceptable in the
   * response. See RFC 2616, section 14.1 for the details.
   */
  bool acceptsMediaType(const std::string& mediaType) const;
};


// Represents an asynchronous in-memory unbuffered Pipe, currently
// used for streaming HTTP responses via chunked encoding. Note that
// being an in-memory pipe means that this cannot be used across OS
// processes.
//
// Much like unix pipes, data is read until end-of-file is
// encountered; this occurs when the the write-end of the pipe is
// closed and there is no outstanding data left to read.
//
// Unlike unix pipes, if the read-end of the pipe is closed before
// the write-end is closed, rather than receiving SIGPIPE or EPIPE
// during a write, the writer is notified via a future. Like unix
// pipes, we are not notified if the read-end is closed after the
// write-end is closed, even if data is remaining in the pipe!
//
// No buffering means that each non-empty write to the pipe will
// correspond to to an equivalent read from the pipe, and the
// reader must "keep up" with the writer in order to avoid
// unbounded memory growth.
//
// The writer can induce a failure on the reader in order to signal
// that an error has occurred. For example, if we are receiving a
// response but a disconnection occurs before the response is
// completed, we want the reader to detect that a disconnection
// occurred!
//
// TODO(bmahler): Consider aggregating writes into larger reads to
// help the reader keep up (a process::Stream abstraction with
// backpressure would obviate the need for this).
//
// TODO(bmahler): Add a more general process::Stream<T> abstraction
// to represent asynchronous finite/infinite streams (possibly
// with "backpressure" on the writer). This is broadly useful
// (e.g. allocator can expose Stream<Allocation>, http::Pipe
// becomes Stream<string>, process::Queue<T> is just an infinite
// Stream<T> (i.e. completion and error semantics hidden)).
class Pipe
{
private:
  struct Data; // Forward declaration.

public:
  class Reader
  {
  public:
    // Returns data written to the pipe.
    // Returns an empty read when end-of-file is reached.
    // Returns Failure if the writer failed, or the read-end
    // is closed.
    Future<std::string> read();

    // Closing the read-end of the pipe before the write-end closes
    // or fails will notify the writer that the reader is no longer
    // interested. Returns false if the read-end was already closed.
    bool close();

    // Comparison operators useful for checking connection equality.
    bool operator==(const Reader& other) const { return data == other.data; }
    bool operator!=(const Reader& other) const { return !(*this == other); }

  private:
    friend class Pipe;

    enum State
    {
      OPEN,
      CLOSED,
    };

    explicit Reader(const std::shared_ptr<Data>& _data) : data(_data) {}

    std::shared_ptr<Data> data;
  };

  class Writer
  {
  public:
    // Returns false if the data could not be written because
    // either end of the pipe was already closed. Note that an
    // empty write has no effect.
    bool write(const std::string& s);

    // Closing the write-end of the pipe will send end-of-file
    // to the reader. Returns false if the write-end of the pipe
    // was already closed or failed.
    bool close();

    // Closes the write-end of the pipe but sends a failure
    // to the reader rather than end-of-file. Returns false
    // if the write-end of the pipe was already closed or failed.
    bool fail(const std::string& message);

    // Returns Nothing when the read-end of the pipe is closed
    // before the write-end is closed, which means the reader
    // was unable to continue reading!
    Future<Nothing> readerClosed() const;

    // Comparison operators useful for checking connection equality.
    bool operator==(const Writer& other) const { return data == other.data; }
    bool operator!=(const Writer& other) const { return !(*this == other); }
  private:
    friend class Pipe;

    enum State
    {
      OPEN,
      CLOSED,
      FAILED,
    };

    explicit Writer(const std::shared_ptr<Data>& _data) : data(_data) {}

    std::shared_ptr<Data> data;
  };

  Pipe() : data(new Data()) {}

  Reader reader() const;
  Writer writer() const;

  // Comparison operators useful for checking connection equality.
  bool operator==(const Pipe& other) const { return data == other.data; }
  bool operator!=(const Pipe& other) const { return !(*this == other); }
private:
  struct Data
  {
    Data()
      : readEnd(Reader::OPEN),
        writeEnd(Writer::OPEN) {}

    // Rather than use a process to serialize access to the pipe's
    // internal data we use a 'std::atomic_flag'.
    std::atomic_flag lock = ATOMIC_FLAG_INIT;

    Reader::State readEnd;
    Writer::State writeEnd;

    // Represents readers waiting for data from the pipe.
    std::queue<Owned<Promise<std::string>>> reads;

    // Represents unread writes in the pipe. Note that we omit
    // empty strings as they serve as a signal for end-of-file.
    std::queue<std::string> writes;

    // Signals when the read-end is closed before the write-end.
    Promise<Nothing> readerClosure;

    // Failure reason when the 'writeEnd' is FAILED.
    Option<Failure> failure;
  };

  std::shared_ptr<Data> data;
};


struct Response
{
  Response()
    : type(NONE)
  {}

  Response(uint16_t _code)
    : type(NONE), code(_code)
  {
    status = Status::string(code);
  }

  explicit Response(
      const std::string& _body,
      uint16_t _code)
    : type(BODY),
      body(_body),
      code(_code)
  {
    headers["Content-Length"] = stringify(body.size());
    status = Status::string(code);
  }

  // TODO(benh): Add major/minor version.
  std::string status;

  Headers headers;

  // Either provide a 'body', an absolute 'path' to a file, or a
  // 'pipe' for streaming a response. Distinguish between the cases
  // using 'type' below.
  //
  // BODY: Uses 'body' as the body of the response. These may be
  // encoded using gzip for efficiency, if 'Content-Encoding' is not
  // already specified.
  //
  // PATH: Attempts to perform a 'sendfile' operation on the file
  // found at 'path'.
  //
  // PIPE: Splices data from the Pipe 'reader' using a "chunked"
  // 'Transfer-Encoding'. The writer uses a Pipe::Writer to
  // perform writes and to detect a closed read-end of the Pipe
  // (i.e. nobody is listening any longer). Once the writer is
  // finished, it will close its end of the pipe to signal end
  // of file to the Reader.
  //
  // In all cases (BODY, PATH, PIPE), you are expected to properly
  // specify the 'Content-Type' header, but the 'Content-Length' and
  // or 'Transfer-Encoding' headers will be filled in for you.
  enum
  {
    NONE,
    BODY,
    PATH,
    PIPE
  } type;

  std::string body;
  std::string path;
  Option<Pipe::Reader> reader;

  uint16_t code;
};


struct OK : Response
{
  OK() : Response(Status::OK) {}

  explicit OK(const char* body)
    : Response(std::string(body), Status::OK) {}

  explicit OK(const std::string& body) : Response(body, Status::OK) {}

  OK(const JSON::Value& value, const Option<std::string>& jsonp = None())
    : Response(Status::OK)
  {
    type = BODY;

    std::ostringstream out;

    if (jsonp.isSome()) {
      out << jsonp.get() << "(";
    }

    out << value;

    if (jsonp.isSome()) {
      out << ");";
      headers["Content-Type"] = "text/javascript";
    } else {
      headers["Content-Type"] = "application/json";
    }

    headers["Content-Length"] = stringify(out.str().size());
    body = out.str().data();
  }
};


struct Accepted : Response
{
  Accepted() : Response(Status::ACCEPTED) {}

  explicit Accepted(const std::string& body)
    : Response(body, Status::ACCEPTED) {}
};


struct TemporaryRedirect : Response
{
  explicit TemporaryRedirect(const std::string& url)
    : Response(Status::TEMPORARY_REDIRECT)
  {
    headers["Location"] = url;
  }
};


struct BadRequest : Response
{
  BadRequest() : Response(Status::BAD_REQUEST) {}

  explicit BadRequest(const std::string& body)
    : Response(body, Status::BAD_REQUEST) {}
};


struct Unauthorized : Response
{
  explicit Unauthorized(const std::vector<std::string>& challenges)
    : Response(Status::UNAUTHORIZED)
  {
    // TODO(arojas): Many HTTP client implementations do not support
    // multiple challenges within a single 'WWW-Authenticate' header.
    // Once MESOS-3306 is fixed, we can use multiple entries for the
    // same header.
    headers["WWW-Authenticate"] = strings::join(", ", challenges);
  }

  Unauthorized(
      const std::vector<std::string>& challenges,
      const std::string& body)
    : Response(body, Status::UNAUTHORIZED)
  {
    // TODO(arojas): Many HTTP client implementations do not support
    // multiple challenges within a single 'WWW-Authenticate' header.
    // Once MESOS-3306 is fixed, we can use multiple entries for the
    // same header.
    headers["WWW-Authenticate"] = strings::join(", ", challenges);
  }

  // TODO(arojas): Remove this in favor of the
  // explicit challenge constructor above.
  explicit Unauthorized(const std::string& realm)
    : Unauthorized(
          std::vector<std::string>{"Basic realm=\"" + realm + "\""}) {}

  // TODO(arojas): Remove this in favor of the
  // explicit challenge constructor above.
  Unauthorized(const std::string& realm, const std::string& body)
    : Unauthorized(
          std::vector<std::string>{"Basic realm=\"" + realm + "\""},
          body) {}
};


struct Forbidden : Response
{
  Forbidden() : Response(Status::FORBIDDEN) {}

  explicit Forbidden(const std::string& body)
    : Response(body, Status::FORBIDDEN) {}
};


struct NotFound : Response
{
  NotFound() : Response(Status::NOT_FOUND) {}

  explicit NotFound(const std::string& body)
    : Response(body, Status::NOT_FOUND) {}
};


struct MethodNotAllowed : Response
{
  // According to RFC 2616, "An Allow header field MUST be present in a
  // 405 (Method Not Allowed) response".

  explicit MethodNotAllowed(
      const std::initializer_list<std::string>& allowedMethods)
    : Response(Status::METHOD_NOT_ALLOWED)
  {
    headers["Allow"] = strings::join(", ", allowedMethods);
  }

  MethodNotAllowed(
      const std::initializer_list<std::string>& allowedMethods,
      const std::string& body)
    : Response(body, Status::METHOD_NOT_ALLOWED)
  {
    headers["Allow"] = strings::join(", ", allowedMethods);
  }
};


struct NotAcceptable : Response
{
  NotAcceptable() : Response(Status::NOT_ACCEPTABLE) {}

  explicit NotAcceptable(const std::string& body)
    : Response(body, Status::NOT_ACCEPTABLE) {}
};


struct Conflict : Response
{
  Conflict() : Response(Status::CONFLICT) {}

  explicit Conflict(const std::string& body)
    : Response(body, Status::CONFLICT) {}
};


struct PreconditionFailed : Response
{
  PreconditionFailed() : Response(Status::PRECONDITION_FAILED) {}

  explicit PreconditionFailed(const std::string& body)
    : Response(body, Status::PRECONDITION_FAILED) {}
};


struct UnsupportedMediaType : Response
{
  UnsupportedMediaType() : Response(Status::UNSUPPORTED_MEDIA_TYPE) {}

  explicit UnsupportedMediaType(const std::string& body)
    : Response(body, Status::UNSUPPORTED_MEDIA_TYPE) {}
};


struct InternalServerError : Response
{
  InternalServerError() : Response(Status::INTERNAL_SERVER_ERROR) {}

  explicit InternalServerError(const std::string& body)
    : Response(body, Status::INTERNAL_SERVER_ERROR) {}
};


struct NotImplemented : Response
{
  NotImplemented() : Response(Status::NOT_IMPLEMENTED) {}

  explicit NotImplemented(const std::string& body)
    : Response(body, Status::NOT_IMPLEMENTED) {}
};


struct ServiceUnavailable : Response
{
  ServiceUnavailable() : Response(Status::SERVICE_UNAVAILABLE) {}

  explicit ServiceUnavailable(const std::string& body)
    : Response(body, Status::SERVICE_UNAVAILABLE) {}
};


namespace path {

// Parses an HTTP path into a map given a pattern (TODO(benh): Make
// the patterns be regular expressions). This returns an error if
// 'pattern' doesn't match 'path'. For example:
//
//   parse("/books/{isbn}/chapters/{chapter}",
//         "/books/0304827484/chapters/3")
//
// Would return a map with the following:
//   books: "books"
//   isbn: "0304827484"
//   chapters: "chapters"
//   chapter: "3"
//
// Another example:
//
//   parse("/books/{isbn}/chapters/{chapter}",
//         "/books/0304827484")
//
// Would return a map with the following:
//   books: "books"
//   isbn: "0304827484"
//
// And another:
//
//   parse("/books/{isbn}/chapters/{chapter}",
//         "/books/0304827484/chapters")
//
// Would return a map with the following:
//   books: "books"
//   isbn: "0304827484"
//   chapters: "chapters"
Try<hashmap<std::string, std::string>> parse(
    const std::string& pattern,
    const std::string& path);

} // namespace path {


// Returns a percent-encoded string according to RFC 3986.
// The input string must not already be percent encoded.
std::string encode(const std::string& s);


// Decodes a percent-encoded string according to RFC 3986.
// The input string must not already be decoded.
// Returns error on the occurrence of a malformed % escape in s.
Try<std::string> decode(const std::string& s);


namespace query {

// Decodes an HTTP query string into a map. For example:
//
//   decode("foo=1&bar=%20&baz&foo=3")
//
// Would return a map with the following:
//   bar: " "
//   baz: ""
//   foo: "3"
//
// We use the last value for a key for simplicity, since the RFC does not
// specify how to handle duplicate keys:
// http://en.wikipedia.org/wiki/Query_string
// TODO(bmahler): If needed, investigate populating the query map inline
// for better performance.
Try<hashmap<std::string, std::string>> decode(const std::string& query);

std::string encode(const hashmap<std::string, std::string>& query);

} // namespace query {


/**
 * Represents a connection to an HTTP server. Pipelining will be
 * used when there are multiple requests in-flight.
 *
 * TODO(bmahler): This does not prevent pipelining with HTTP/1.0.
 */
class Connection
{
public:
  Connection() = delete;

  /**
   * Sends a request to the server. If there are additional requests
   * in flight, pipelining will occur. If 'streamedResponse' is set,
   * the response body will be of type 'PIPE'. Note that if the
   * request or response has a 'Connection: close' header, the
   * connection will close after the response completes.
   */
  Future<Response> send(const Request& request, bool streamedResponse = false);

  /**
   * Disconnects from the server.
   */
  Future<Nothing> disconnect();

  /**
   * Returns a future that is satisfied when a disconnection occurs.
   */
  Future<Nothing> disconnected();

  bool operator==(const Connection& c) const { return data == c.data; }
  bool operator!=(const Connection& c) const { return !(*this == c); }

private:
  Connection(const network::Socket& s);
  friend Future<Connection> connect(const URL&);

  // Forward declaration.
  struct Data;

  std::shared_ptr<Data> data;
};


Future<Connection> connect(const URL& url);


// TODO(bmahler): Consolidate these functions into a single
// http::request function that takes a 'Request' object.

// TODO(bmahler): Support discarding the future responses;
// discarding should disconnect from the server.

// TODO(joerg84): Make names consistent (see Mesos-3256).

// Asynchronously sends an HTTP GET request to the specified URL
// and returns the HTTP response of type 'BODY' once the entire
// response is received.
Future<Response> get(
    const URL& url,
    const Option<Headers>& headers = None());


// Asynchronously sends an HTTP GET request to the process with the
// given UPID and returns the HTTP response of type 'BODY' once the
// entire response is received.
Future<Response> get(
    const UPID& upid,
    const Option<std::string>& path = None(),
    const Option<std::string>& query = None(),
    const Option<Headers>& headers = None());


// Asynchronously sends an HTTP POST request to the specified URL
// and returns the HTTP response of type 'BODY' once the entire
// response is received.
Future<Response> post(
    const URL& url,
    const Option<Headers>& headers = None(),
    const Option<std::string>& body = None(),
    const Option<std::string>& contentType = None());


// Asynchronously sends an HTTP POST request to the process with the
// given UPID and returns the HTTP response of type 'BODY' once the
// entire response is received.
Future<Response> post(
    const UPID& upid,
    const Option<std::string>& path = None(),
    const Option<Headers>& headers = None(),
    const Option<std::string>& body = None(),
    const Option<std::string>& contentType = None());


/**
 * Asynchronously sends an HTTP DELETE request to the process with the
 * given UPID and returns the HTTP response.
 *
 * @param url The target url for the request.
 * @param headers Optional header for the request.
 * @return A future with the HTTP response.
 */
Future<Response> requestDelete(
    const URL& url,
    const Option<Headers>& headers = None());


/**
 * Asynchronously sends an HTTP DELETE request to the process with the
 * given UPID and returns the HTTP response.
 *
 * @param upid The target process's assigned untyped PID.
 * @param path The optional path to be be deleted. If not send the
     request is send to the process directly.
 * @param headers Optional headers for the request.
 * @return A future with the HTTP response.
 */
Future<Response> requestDelete(
    const UPID& upid,
    const Option<std::string>& path = None(),
    const Option<Headers>& headers = None());


namespace streaming {

// Asynchronously sends an HTTP GET request to the specified URL
// and returns the HTTP response of type 'PIPE' once the response
// headers are received. The caller must read the response body
// from the Pipe::Reader.
Future<Response> get(
    const URL& url,
    const Option<Headers>& headers = None());

// Asynchronously sends an HTTP GET request to the process with the
// given UPID and returns the HTTP response of type 'PIPE' once the
// response headers are received. The caller must read the response
// body from the Pipe::Reader.
Future<Response> get(
    const UPID& upid,
    const Option<std::string>& path = None(),
    const Option<std::string>& query = None(),
    const Option<Headers>& headers = None());

// Asynchronously sends an HTTP POST request to the specified URL
// and returns the HTTP response of type 'PIPE' once the response
// headers are received. The caller must read the response body
// from the Pipe::Reader.
Future<Response> post(
    const URL& url,
    const Option<Headers>& headers = None(),
    const Option<std::string>& body = None(),
    const Option<std::string>& contentType = None());

// Asynchronously sends an HTTP POST request to the process with the
// given UPID and returns the HTTP response of type 'PIPE' once the
// response headers are received. The caller must read the response
// body from the Pipe::Reader.
Future<Response> post(
    const UPID& upid,
    const Option<std::string>& path = None(),
    const Option<Headers>& headers = None(),
    const Option<std::string>& body = None(),
    const Option<std::string>& contentType = None());

} // namespace streaming {

} // namespace http {
} // namespace process {

#endif // __PROCESS_HTTP_HPP__
