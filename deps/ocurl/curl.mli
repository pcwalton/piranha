(*
 * curl.mli
 *
 * Copyright (c) 2003-2008, Lars Nilsson, <lars@quantumchamaeleon.com>
 *)

type t

type curlCode =
  | CURLE_OK
  | CURLE_UNSUPPORTED_PROTOCOL
  | CURLE_FAILED_INIT
  | CURLE_URL_MALFORMAT
  | CURLE_URL_MALFORMAT_USER
  | CURLE_COULDNT_RESOLVE_PROXY
  | CURLE_COULDNT_RESOLVE_HOST
  | CURLE_COULDNT_CONNECT
  | CURLE_FTP_WEIRD_SERVER_REPLY
  | CURLE_FTP_ACCESS_DENIED
  | CURLE_FTP_USER_PASSWORD_INCORRECT
  | CURLE_FTP_WEIRD_PASS_REPLY
  | CURLE_FTP_WEIRD_USER_REPLY
  | CURLE_FTP_WEIRD_PASV_REPLY
  | CURLE_FTP_WEIRD_227_FORMAT
  | CURLE_FTP_CANT_GET_HOST
  | CURLE_FTP_CANT_RECONNECT
  | CURLE_FTP_COULDNT_SET_BINARY
  | CURLE_PARTIAL_FILE
  | CURLE_FTP_COULDNT_RETR_FILE
  | CURLE_FTP_WRITE_ERROR
  | CURLE_FTP_QUOTE_ERROR
  | CURLE_HTTP_NOT_FOUND
  | CURLE_WRITE_ERROR
  | CURLE_MALFORMAT_USER
  | CURLE_FTP_COULDNT_STOR_FILE
  | CURLE_READ_ERROR
  | CURLE_OUT_OF_MEMORY
  | CURLE_OPERATION_TIMEOUTED
  | CURLE_FTP_COULDNT_SET_ASCII
  | CURLE_FTP_PORT_FAILED
  | CURLE_FTP_COULDNT_USE_REST
  | CURLE_FTP_COULDNT_GET_SIZE
  | CURLE_HTTP_RANGE_ERROR
  | CURLE_HTTP_POST_ERROR
  | CURLE_SSL_CONNECT_ERROR
  | CURLE_FTP_BAD_DOWNLOAD_RESUME
  | CURLE_FILE_COULDNT_READ_FILE
  | CURLE_LDAP_CANNOT_BIND
  | CURLE_LDAP_SEARCH_FAILED
  | CURLE_LIBRARY_NOT_FOUND
  | CURLE_FUNCTION_NOT_FOUND
  | CURLE_ABORTED_BY_CALLBACK
  | CURLE_BAD_FUNCTION_ARGUMENT
  | CURLE_BAD_CALLING_ORDER
  | CURLE_HTTP_PORT_FAILED
  | CURLE_BAD_PASSWORD_ENTERED
  | CURLE_TOO_MANY_REDIRECTS
  | CURLE_UNKNOWN_TELNET_OPTION
  | CURLE_TELNET_OPTION_SYNTAX
  | CURLE_OBSOLETE
  | CURLE_SSL_PEER_CERTIFICATE
  | CURLE_GOT_NOTHING
  | CURLE_SSL_ENGINE_NOTFOUND
  | CURLE_SSL_ENGINE_SETFAILED
  | CURLE_SEND_ERROR
  | CURLE_RECV_ERROR
  | CURLE_SHARE_IN_USE
  | CURLE_SSL_CERTPROBLEM
  | CURLE_SSL_CIPHER
  | CURLE_SSL_CACERT
  | CURLE_BAD_CONTENT_ENCODING
  | CURLE_LDAP_INVALID_URL
  | CURLE_FILESIZE_EXCEEDED
  | CURLE_FTP_SSL_FAILED
  | CURLE_USE_SSL_FAILED
  | CURLE_SEND_FAIL_REWIND
  | CURLE_SSL_ENGINE_INITFAILED
  | CURLE_LOGIN_DENIED
  | CURLE_TFTP_NOTFOUND
  | CURLE_TFTP_PERM
  | CURLE_REMOTE_DISK_FULL
  | CURLE_TFTP_ILLEGAL
  | CURLE_TFTP_UNKNOWNID
  | CURLE_REMOTE_FILE_EXISTS
  | CURLE_TFTP_NOSUCHUSER
  | CURLE_CONV_FAILED
  | CURLE_CONV_REQD
  | CURLE_SSL_CACERT_BADFILE
  | CURLE_REMOTE_FILE_NOT_FOUND
  | CURLE_SSH
  | CURLE_SSL_SHUTDOWN_FAILED
  | CURLE_AGAIN

exception CurlException of (curlCode * int * string)

type curlNETRCOption =
  | CURL_NETRC_OPTIONAL
  | CURL_NETRC_IGNORED
  | CURL_NETRC_REQUIRED

type curlEncoding =
  | CURL_ENCODING_NONE
  | CURL_ENCODING_DEFLATE

type curlContentType =
  | DEFAULT
  | CONTENTTYPE of string

type curlHTTPPost =
  | CURLFORM_CONTENT of string * string * curlContentType
  | CURLFORM_FILECONTENT of string * string * curlContentType
  | CURLFORM_FILE of string * string * curlContentType
  | CURLFORM_BUFFER of string * string * string * curlContentType

(*      
type curlHTTPPost =
  | CURLFORM_COPYNAME of string
  | CURLFORM_PTRNAME of string
  | CURLFORM_NAMELENGTH of int
  | CURLFORM_COPYCONTENTS of string
  | CURLFORM_PTRCONTENTS of string
  | CURLFORM_FILE of string
  | CURLFORM_FILECONTENT of string
  | CURLFORM_CONTENTSLENGTH of int
  | CURLFORM_CONTENTTYPE of string
*)

type curlTimeCondition =
  | TIMECOND_IFMODSINCE
  | TIMECOND_IFUNMODSINCE
      
type curlKRB4Level =
  | KRB4_NONE
  | KRB4_CLEAR
  | KRB4_SAFE
  | KRB4_CONFIDENTIAL
  | KRB4_PRIVATE

type curlClosePolicy =
  | CLOSEPOLICY_OLDEST
  | CLOSEPOLICY_LEAST_RECENTLY_USED
      
type curlSSLVerifyHost =
  | SSLVERIFYHOST_EXISTENCE
  | SSLVERIFYHOST_HOSTNAME
      
type curlHTTPVersion =
  | HTTP_VERSION_NONE
  | HTTP_VERSION_1_0
  | HTTP_VERSION_1_1
      
type curlDebugType =
  | DEBUGTYPE_TEXT
  | DEBUGTYPE_HEADER_IN
  | DEBUGTYPE_HEADER_OUT
  | DEBUGTYPE_DATA_IN
  | DEBUGTYPE_DATA_OUT
  | DEBUGTYPE_END

type curlAuth =
  | CURLAUTH_BASIC
  | CURLAUTH_DIGEST
  | CURLAUTH_GSSNEGOTIATE
  | CURLAUTH_NTLM
  | CURLAUTH_ANY
  | CURLAUTH_ANYSAFE

type curlIPResolve =
  | IPRESOLVE_WHATEVER
  | IPRESOLVE_V4
  | IPRESOLVE_V6

type curlFTPSSL =
  | FTPSSL_NONE
  | FTPSSL_TRY
  | FTPSSL_CONTROL
  | FTPSSL_ALL

type curlFTPSSLAuth =
  | FTPAUTH_DEFAULT
  | FTPAUTH_SSL
  | FTPAUTH_TLS

type curlIOCmd =
  | IOCMD_NOP
  | IOCMD_RESTARTREAD

type curlIOErr =
  | IOE_OK
  | IOE_UNKNOWNCMD
  | IOE_FAILRESTART

type curlFTPMethod =
  | FTPMETHOD_DEFAULT
  | FTPMETHOD_MULTICWD
  | FTPMETHOD_NOCWD
  | FTPMETHOD_SINGLECWD

type curlSSHAuthTypes =
  | SSHAUTH_ANY
  | SSHAUTH_PUBLICKEY
  | SSHAUTH_PASSWORD
  | SSHAUTH_HOST
  | SSHAUTH_KEYBOARD

type curlFTPSSLCCC =
  | FTPSSL_CCC_NONE
  | FTPSSL_CCC_PASSIVE
  | FTPSSL_CCC_ACTIVE

type curlSeek =
  | SEEK_SET
  | SEEK_CUR
  | SEEK_END

type curlOption =
  | CURLOPT_WRITEFUNCTION of (string -> int)
  | CURLOPT_READFUNCTION of (int -> string)
  | CURLOPT_INFILESIZE of int
  | CURLOPT_URL of string
  | CURLOPT_PROXY of string
  | CURLOPT_PROXYPORT of int
  | CURLOPT_HTTPPROXYTUNNEL of bool
  | CURLOPT_VERBOSE of bool
  | CURLOPT_HEADER of bool
  | CURLOPT_NOPROGRESS of bool
  | CURLOPT_NOSIGNAL of bool
  | CURLOPT_NOBODY of bool
  | CURLOPT_FAILONERROR of bool
  | CURLOPT_UPLOAD of bool
  | CURLOPT_POST of bool
  | CURLOPT_FTPLISTONLY of bool
  | CURLOPT_FTPAPPEND of bool
  | CURLOPT_NETRC of curlNETRCOption
  | CURLOPT_ENCODING of curlEncoding
  | CURLOPT_FOLLOWLOCATION of bool
  | CURLOPT_TRANSFERTEXT of bool
  | CURLOPT_PUT of bool
  | CURLOPT_USERPWD of string
  | CURLOPT_PROXYUSERPWD of string
  | CURLOPT_RANGE of string
  | CURLOPT_ERRORBUFFER of string ref
  | CURLOPT_TIMEOUT of int
  | CURLOPT_POSTFIELDS of string
  | CURLOPT_POSTFIELDSIZE of int
  | CURLOPT_REFERER of string
  | CURLOPT_USERAGENT of string
  | CURLOPT_FTPPORT of string
  | CURLOPT_LOWSPEEDLIMIT of int
  | CURLOPT_LOWSPEEDTIME of int
  | CURLOPT_RESUMEFROM of int
  | CURLOPT_COOKIE of string
  | CURLOPT_HTTPHEADER of string list
  | CURLOPT_HTTPPOST of curlHTTPPost list
  | CURLOPT_SSLCERT of string
  | CURLOPT_SSLCERTTYPE of string
  | CURLOPT_SSLCERTPASSWD of string
  | CURLOPT_SSLKEY of string
  | CURLOPT_SSLKEYTYPE of string
  | CURLOPT_SSLKEYPASSWD of string
  | CURLOPT_SSLENGINE of string
  | CURLOPT_SSLENGINEDEFAULT of bool
  | CURLOPT_CRLF of bool
  | CURLOPT_QUOTE of string list
  | CURLOPT_POSTQUOTE of string list
  | CURLOPT_HEADERFUNCTION of (string -> int)
  | CURLOPT_COOKIEFILE of string
  | CURLOPT_SSLVERSION of int
  | CURLOPT_TIMECONDITION of curlTimeCondition
  | CURLOPT_TIMEVALUE of int32
  | CURLOPT_CUSTOMREQUEST of string
  | CURLOPT_STDERR (* UNIMPLEMENTED *)
  | CURLOPT_INTERFACE of string
  | CURLOPT_KRB4LEVEL of curlKRB4Level
  | CURLOPT_PROGRESSFUNCTION of (float -> float -> float -> float -> bool)
  | CURLOPT_SSLVERIFYPEER of bool
  | CURLOPT_CAINFO of string
  | CURLOPT_CAPATH of string
  | CURLOPT_FILETIME of bool
  | CURLOPT_MAXREDIRS of int
  | CURLOPT_MAXCONNECTS of int
  | CURLOPT_CLOSEPOLICY of curlClosePolicy
  | CURLOPT_FRESHCONNECT of bool
  | CURLOPT_FORBIDREUSE of bool
  | CURLOPT_RANDOMFILE of string
  | CURLOPT_EGDSOCKET of string
  | CURLOPT_CONNECTTIMEOUT of int
  | CURLOPT_HTTPGET of bool
  | CURLOPT_SSLVERIFYHOST of curlSSLVerifyHost
  | CURLOPT_COOKIEJAR of string
  | CURLOPT_SSLCIPHERLIST of string
  | CURLOPT_HTTPVERSION of curlHTTPVersion
  | CURLOPT_FTPUSEEPSV of bool
  | CURLOPT_DNSCACHETIMEOUT of int
  | CURLOPT_DNSUSEGLOBALCACHE of bool
  | CURLOPT_DEBUGFUNCTION of (t -> curlDebugType -> string -> unit)
  | CURLOPT_PRIVATE of string
  | CURLOPT_HTTP200ALIASES of string list
  | CURLOPT_UNRESTRICTEDAUTH of bool
  | CURLOPT_FTPUSEEPRT of bool
  | CURLOPT_HTTPAUTH of curlAuth list
  | CURLOPT_FTPCREATEMISSINGDIRS of bool
  | CURLOPT_PROXYAUTH of curlAuth list
  | CURLOPT_FTPRESPONSETIMEOUT of int
  | CURLOPT_IPRESOLVE of curlIPResolve
  | CURLOPT_MAXFILESIZE of int32
  | CURLOPT_INFILESIZELARGE of int64
  | CURLOPT_RESUMEFROMLARGE of int64
  | CURLOPT_MAXFILESIZELARGE of int64
  | CURLOPT_NETRCFILE of string
  | CURLOPT_FTPSSL of curlFTPSSL
  | CURLOPT_POSTFIELDSIZELARGE of int64
  | CURLOPT_TCPNODELAY of bool
  | CURLOPT_FTPSSLAUTH of curlFTPSSLAuth
  | CURLOPT_IOCTLFUNCTION of (t -> curlIOCmd -> curlIOErr)
  | CURLOPT_FTPACCOUNT of string
  | CURLOPT_COOKIELIST of string
  | CURLOPT_IGNORECONTENTLENGTH of bool
  | CURLOPT_FTPSKIPPASVIP of bool
  | CURLOPT_FTPFILEMETHOD of curlFTPMethod
  | CURLOPT_LOCALPORT of int
  | CURLOPT_LOCALPORTRANGE of int
  | CURLOPT_CONNECTONLY of bool
  | CURLOPT_MAXSENDSPEEDLARGE of int64
  | CURLOPT_MAXRECVSPEEDLARGE of int64
  | CURLOPT_FTPALTERNATIVETOUSER of string
  | CURLOPT_SSLSESSIONIDCACHE of bool
  | CURLOPT_SSHAUTHTYPES of curlSSHAuthTypes list
  | CURLOPT_SSHPUBLICKEYFILE of string
  | CURLOPT_SSHPRIVATEKEYFILE of string
  | CURLOPT_FTPSSLCCC of curlFTPSSLCCC
  | CURLOPT_TIMEOUTMS of int
  | CURLOPT_CONNECTTIMEOUTMS of int
  | CURLOPT_HTTPTRANSFERDECODING of bool
  | CURLOPT_HTTPCONTENTDECODING of bool
  | CURLOPT_NEWFILEPERMS of int
  | CURLOPT_NEWDIRECTORYPERMS of int
  | CURLOPT_POST301 of bool
  | CURLOPT_SSHHOSTPUBLICKEYMD5 of string
  | CURLOPT_COPYPOSTFIELDS of string
  | CURLOPT_PROXYTRANSFERMODE of bool
  | CURLOPT_SEEKFUNCTION of (int64 -> curlSeek -> int)

type initOption =
  | CURLINIT_GLOBALALL
  | CURLINIT_GLOBALSSL
  | CURLINIT_GLOBALWIN32
  | CURLINIT_GLOBALNOTHING

type curlInfo =
  | CURLINFO_EFFECTIVE_URL
  | CURLINFO_HTTP_CODE
  | CURLINFO_RESPONSE_CODE
  | CURLINFO_TOTAL_TIME
  | CURLINFO_NAMELOOKUP_TIME
  | CURLINFO_CONNECT_TIME
  | CURLINFO_PRETRANSFER_TIME
  | CURLINFO_SIZE_UPLOAD
  | CURLINFO_SIZE_DOWNLOAD
  | CURLINFO_SPEED_DOWNLOAD
  | CURLINFO_SPEED_UPLOAD
  | CURLINFO_HEADER_SIZE
  | CURLINFO_REQUEST_SIZE
  | CURLINFO_SSL_VERIFYRESULT
  | CURLINFO_FILETIME
  | CURLINFO_CONTENT_LENGTH_DOWNLOAD
  | CURLINFO_CONTENT_LENGTH_UPLOAD
  | CURLINFO_STARTTRANSFER_TIME
  | CURLINFO_CONTENT_TYPE
  | CURLINFO_REDIRECT_TIME
  | CURLINFO_REDIRECT_COUNT
  | CURLINFO_PRIVATE
  | CURLINFO_HTTP_CONNECTCODE
  | CURLINFO_HTTPAUTH_AVAIL
  | CURLINFO_PROXYAUTH_AVAIL
  | CURLINFO_OS_ERRNO
  | CURLINFO_NUM_CONNECTS
  | CURLINFO_SSL_ENGINES
  | CURLINFO_COOKIELIST
  | CURLINFO_LASTSOCKET
  | CURLINFO_FTP_ENTRY_PATH

type curlInfoResult =
  | CURLINFO_String of string
  | CURLINFO_Long of int
  | CURLINFO_Double of float
  | CURLINFO_StringList of string list

val global_init : initOption -> unit
val global_cleanup : unit -> unit
val init : unit -> t
val duphandle : t -> t
val setopt : t -> curlOption -> unit
val perform : t -> unit
val cleanup : t -> unit
val getinfo : t -> curlInfo -> curlInfoResult
val escape : string -> string
val unescape : string -> string
val getdate : string -> float -> float
val version : unit -> string

val set_writefunction : t -> (string -> int) -> unit
val set_readfunction : t -> (int -> string) -> unit
val set_infilesize : t -> int -> unit
val set_url : t -> string -> unit
val set_proxy : t -> string -> unit
val set_proxyport : t -> int -> unit
val set_httpproxytunnel : t -> bool -> unit
val set_verbose : t -> bool -> unit
val set_header : t -> bool -> unit
val set_noprogress : t -> bool -> unit
val set_nosignal : t -> bool -> unit
val set_nobody : t -> bool -> unit
val set_failonerror : t -> bool -> unit
val set_upload : t -> bool -> unit
val set_post : t -> bool -> unit
val set_ftplistonly : t -> bool -> unit
val set_ftpappend : t -> bool -> unit
val set_netrc : t -> curlNETRCOption -> unit
val set_encoding : t -> curlEncoding -> unit
val set_followlocation : t -> bool -> unit
val set_transfertext  : t -> bool -> unit
val set_put : t -> bool -> unit
val set_userpwd : t -> string -> unit
val set_proxyuserpwd : t -> string -> unit
val set_range : t -> string -> unit
val set_errorbuffer : t -> string ref -> unit
val set_timeout : t -> int -> unit
val set_postfields : t -> string -> unit
val set_postfieldsize : t -> int -> unit
val set_referer : t -> string -> unit
val set_useragent : t -> string -> unit
val set_ftpport : t -> string -> unit
val set_lowspeedlimit : t -> int -> unit
val set_lowspeedtime : t -> int -> unit
val set_resumefrom : t -> int -> unit
val set_cookie : t -> string -> unit
val set_httpheader : t -> string list -> unit
val set_httppost : t -> curlHTTPPost list -> unit
val set_sslcert : t -> string -> unit
val set_sslcerttype : t -> string -> unit
val set_sslcertpasswd : t -> string -> unit
val set_sslkey : t -> string -> unit
val set_sslkeytype : t -> string -> unit
val set_sslkeypasswd : t -> string -> unit
val set_sslengine : t -> string -> unit
val set_sslenginedefault : t -> bool -> unit
val set_crlf : t -> bool -> unit
val set_quote : t -> string list -> unit
val set_postquote : t -> string list -> unit
val set_headerfunction: t -> (string -> int) -> unit
val set_cookiefile : t -> string -> unit
val set_sslversion : t -> int -> unit
val set_timecondition : t -> curlTimeCondition -> unit
val set_timevalue : t -> int32 -> unit
val set_customrequest : t -> string -> unit
val set_interface : t -> string -> unit
val set_krb4level : t -> curlKRB4Level -> unit
val set_progressfunction : t -> (float -> float -> float -> float -> bool) -> unit
val set_sslverifypeer : t -> bool -> unit
val set_cainfo : t -> string -> unit
val set_capath : t -> string -> unit
val set_filetime : t -> bool -> unit
val set_maxredirs : t -> int -> unit
val set_maxconnects : t -> int -> unit
val set_closepolicy : t -> curlClosePolicy -> unit
val set_freshconnect : t -> bool -> unit
val set_forbidreuse : t -> bool -> unit
val set_randomfile : t -> string -> unit
val set_egdsocket : t -> string -> unit
val set_connecttimeout : t -> int -> unit
val set_httpget : t -> bool -> unit
val set_sslverifyhost : t -> curlSSLVerifyHost -> unit
val set_cookiejar : t -> string -> unit
val set_sslcipherlist : t -> string -> unit
val set_httpversion : t -> curlHTTPVersion -> unit
val set_ftpuseepsv : t -> bool -> unit
val set_dnscachetimeout : t -> int -> unit
val set_dnsuseglobalcache : t -> bool -> unit
val set_debugfunction : t -> (t -> curlDebugType -> string -> unit) -> unit
val set_private : t -> string -> unit
val set_http200aliases : t -> string list -> unit
val set_unrestrictedauth : t -> bool -> unit
val set_ftpuseeprt : t -> bool -> unit
val set_httpauth : t -> curlAuth list -> unit
val set_ftpcreatemissingdirs : t -> bool -> unit
val set_proxyauth : t -> curlAuth list -> unit
val set_ftpresponsetimeout : t -> int -> unit
val set_ipresolve : t -> curlIPResolve -> unit
val set_maxfilesize : t -> int32 -> unit
val set_infilesizelarge : t -> int64 -> unit
val set_resumefromlarge : t -> int64 -> unit
val set_maxfilesizelarge : t -> int64 -> unit
val set_netrcfile : t -> string -> unit
val set_ftpssl : t -> curlFTPSSL -> unit
val set_postfieldsizelarge : t -> int64 -> unit
val set_tcpnodelay : t -> bool -> unit
val set_ftpsslauth : t -> curlFTPSSLAuth -> unit
val set_ioctlfunction : t -> (t -> curlIOCmd -> curlIOErr) -> unit
val set_ftpaccount : t -> string -> unit
val set_cookielist : t -> string -> unit
val set_ignorecontentlength : t -> bool -> unit
val set_ftpskippasvip : t -> bool -> unit
val set_ftpfilemethod : t -> curlFTPMethod -> unit
val set_localport : t -> int -> unit
val set_localportrange : t -> int -> unit
val set_connectonly : t -> bool -> unit
val set_maxsendspeedlarge : t -> int64 -> unit
val set_maxrecvspeedlarge : t -> int64 -> unit
val set_ftpalternativetouser : t -> string -> unit
val set_sslsessionidcache : t -> bool -> unit
val set_sshauthtypes : t -> curlSSHAuthTypes list -> unit
val set_sshpublickeyfile : t -> string -> unit
val set_ftpsslccc : t -> curlFTPSSLCCC -> unit
val set_timeoutms : t -> int -> unit
val set_connecttimeoutms : t -> int -> unit
val set_httptransferdecoding : t -> bool -> unit
val set_httpcontentdecoding : t -> bool -> unit
val set_newfileperms : t -> int -> unit
val set_newdirectoryperms : t -> int -> unit
val set_post301 : t -> bool -> unit
val set_sshhostpublickeymd5 : t -> string -> unit
val set_copypostfields : t -> string -> unit
val set_proxytransfermode : t -> bool -> unit
val set_seekfunction : t -> (int64 -> curlSeek -> int) -> unit

val get_effectiveurl : t -> string
val get_httpcode : t -> int
val get_responsecode : t -> int
val get_totaltime : t -> float
val get_namelookuptime : t -> float
val get_connecttime : t -> float
val get_pretransfertime : t -> float
val get_sizeupload : t -> float
val get_sizedownload : t -> float
val get_speeddownload : t -> float
val get_speedupload : t -> float
val get_headersize : t -> int
val get_requestsize : t -> int
val get_sslverifyresult : t -> int
val get_filetime : t -> float
val get_contentlengthdownload : t -> float
val get_contentlengthupload : t -> float
val get_starttransfertime : t -> float
val get_contenttype : t -> string
val get_redirecttime : t -> float
val get_redirectcount : t -> int
val get_private : t -> string
val get_httpconnectcode : t -> int
val get_httpauthavail : t -> curlAuth list
val get_proxyauthavail : t -> curlAuth list
val get_oserrno : t -> int
val get_numconnects : t -> int
val get_sslengines : t -> string list
val get_cookielist : t -> string list
val get_lastsocket : t -> int
val get_ftpentrypath : t -> string

class handle :
  object ('a)
    val conn : t
    method cleanup : unit
    method duphandle : 'a
    method perform : unit

    method set_writefunction : (string -> int) -> unit
    method set_readfunction : (int -> string) -> unit
    method set_infilesize : int -> unit
    method set_url : string -> unit
    method set_proxy : string -> unit
    method set_proxyport : int -> unit
    method set_httpproxytunnel : bool -> unit
    method set_verbose : bool -> unit
    method set_header : bool -> unit
    method set_noprogress : bool -> unit
    method set_nosignal : bool -> unit
    method set_nobody : bool -> unit
    method set_failonerror : bool -> unit
    method set_upload : bool -> unit
    method set_post : bool -> unit
    method set_ftplistonly : bool -> unit
    method set_ftpappend : bool -> unit
    method set_netrc : curlNETRCOption -> unit
    method set_encoding : curlEncoding -> unit
    method set_followlocation : bool -> unit
    method set_transfertext  : bool -> unit
    method set_put : bool -> unit
    method set_userpwd : string -> unit
    method set_proxyuserpwd : string -> unit
    method set_range : string -> unit
    method set_errorbuffer : string ref -> unit
    method set_timeout : int -> unit
    method set_postfields : string -> unit
    method set_postfieldsize : int -> unit
    method set_referer : string -> unit
    method set_useragent : string -> unit
    method set_ftpport : string -> unit
    method set_lowspeedlimit : int -> unit
    method set_lowspeedtime : int -> unit
    method set_resumefrom : int -> unit
    method set_cookie : string -> unit
    method set_httpheader : string list -> unit
    method set_httppost : curlHTTPPost list -> unit
    method set_sslcert : string -> unit
    method set_sslcerttype : string -> unit
    method set_sslcertpasswd : string -> unit
    method set_sslkey : string -> unit
    method set_sslkeytype : string -> unit
    method set_sslkeypasswd : string -> unit
    method set_sslengine : string -> unit
    method set_sslenginedefault : bool -> unit
    method set_crlf : bool -> unit
    method set_quote : string list -> unit
    method set_postquote : string list -> unit
    method set_headerfunction: (string -> int) -> unit
    method set_cookiefile : string -> unit
    method set_sslversion : int -> unit
    method set_timecondition : curlTimeCondition -> unit
    method set_timevalue : int32 -> unit
    method set_customrequest : string -> unit
    method set_interface : string -> unit
    method set_krb4level : curlKRB4Level -> unit
    method set_progressfunction :
      (float -> float -> float -> float -> bool) -> unit
    method set_sslverifypeer : bool -> unit
    method set_cainfo : string -> unit
    method set_capath : string -> unit
    method set_filetime : bool -> unit
    method set_maxredirs : int -> unit
    method set_maxconnects : int -> unit
    method set_closepolicy : curlClosePolicy -> unit
    method set_freshconnect : bool -> unit
    method set_forbidreuse : bool -> unit
    method set_randomfile : string -> unit
    method set_egdsocket : string -> unit
    method set_connecttimeout : int -> unit
    method set_httpget : bool -> unit
    method set_sslverifyhost : curlSSLVerifyHost -> unit
    method set_cookiejar : string -> unit
    method set_sslcipherlist : string -> unit
    method set_httpversion : curlHTTPVersion -> unit
    method set_ftpuseepsv : bool -> unit
    method set_dnscachetimeout : int -> unit
    method set_dnsuseglobalcache : bool -> unit
    method set_debugfunction : (t -> curlDebugType -> string -> unit) -> unit
    method set_private : string -> unit
    method set_http200aliases : string list -> unit
    method set_unrestrictedauth : bool -> unit
    method set_ftpuseeprt : bool -> unit
    method set_httpauth : curlAuth list -> unit
    method set_ftpcreatemissingdirs : bool -> unit
    method set_proxyauth : curlAuth list -> unit
    method set_ftpresponsetimeout : int -> unit
    method set_ipresolve : curlIPResolve -> unit
    method set_maxfilesize : int32 -> unit
    method set_infilesizelarge : int64 -> unit
    method set_resumefromlarge : int64 -> unit
    method set_maxfilesizelarge : int64 -> unit
    method set_netrcfile : string -> unit
    method set_ftpssl : curlFTPSSL -> unit
    method set_postfieldsizelarge : int64 -> unit
    method set_tcpnodelay : bool -> unit
    method set_ftpsslauth : curlFTPSSLAuth -> unit
    method set_ioctlfunction : (t -> curlIOCmd -> curlIOErr) -> unit
    method set_ftpaccount : string -> unit 
    method set_cookielist : string -> unit
    method set_ignorecontentlength : bool -> unit
    method set_ftpskippasvip : bool -> unit
    method set_ftpfilemethod : curlFTPMethod -> unit
    method set_localport : int -> unit
    method set_localportrange : int -> unit
    method set_connectonly : bool -> unit
    method set_maxsendspeedlarge : int64 -> unit
    method set_maxrecvspeedlarge : int64 -> unit
    method set_ftpalternativetouser : string -> unit
    method set_sslsessionidcache : bool -> unit
    method set_sshauthtypes : curlSSHAuthTypes list -> unit
    method set_sshpublickeyfile : string -> unit
    method set_sshprivatekeyfile : string -> unit
    method set_ftpsslccc : curlFTPSSLCCC -> unit
    method set_timeoutms : int -> unit
    method set_connecttimeoutms : int -> unit
    method set_httptransferdecoding : bool -> unit
    method set_httpcontentdecoding : bool -> unit
    method set_newfileperms : int -> unit
    method set_newdirectoryperms : int -> unit
    method set_post301 : bool -> unit
    method set_sshhostpublickeymd5 : string -> unit
    method set_copypostfields : string -> unit
    method set_proxytransfermode : bool -> unit
    method set_seekfunction : (int64 -> curlSeek -> int) -> unit

    method get_effectiveurl : string
    method get_httpcode : int
    method get_responsecode : int
    method get_totaltime : float
    method get_namelookuptime : float
    method get_connecttime : float
    method get_pretransfertime : float
    method get_sizeupload : float
    method get_sizedownload : float
    method get_speeddownload : float
    method get_speedupload : float
    method get_headersize : int
    method get_requestsize : int
    method get_sslverifyresult : int
    method get_filetime : float
    method get_contentlengthdownload : float
    method get_contentlengthupload : float
    method get_starttransfertime : float
    method get_contenttype : string
    method get_redirecttime : float
    method get_redirectcount : int
    method get_private : string
    method get_httpconnectcode : int
    method get_httpauthavail : curlAuth list
    method get_proxyauthavail : curlAuth list
    method get_oserrno : int
    method get_numconnects : int
    method get_sslengines : string list
    method get_cookielist : string list
    method get_lastsocket : int
    method get_ftpentrypath : string
  end
