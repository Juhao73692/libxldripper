// Request URL and response handling adapted from XLDCueParser.m and
// XLDAccurateRipDB.m, XLD SVN r652. Copyright 2006–2008 tmkk.
// Access to AccurateRip is regulated, see http://www.accuraterip.com/3rdparty-access.htm
// Modified for libxldripper. See README.md for attribution.

#import <Foundation/Foundation.h>
#include <xld/ripper.h>

__attribute__((objc_runtime_name("LibXLDRipperAccurateRipQuery")))
@interface XLDAccurateRipQuery : NSObject <NSURLSessionDataDelegate>
- (instancetype)initWithDisc:(const xld_ripper_disc_info *)disc
                    callback:(xld_ripper_accuraterip_callback)callback context:(void *)context;
- (BOOL)prepareTaskInSession:(NSURLSession *)session;
- (void)resume;
- (void)cancel;
@end

@implementation XLDAccurateRipQuery {
    xld_ripper_disc_info _disc;
    xld_ripper_accuraterip_callback _callback;
    void *_context;
    NSMutableData *_record;
    NSURLSessionDataTask *_task;
    xld_ripper_status _status;
    int32_t _httpStatus;
}

- (instancetype)initWithDisc:(const xld_ripper_disc_info *)disc
                    callback:(xld_ripper_accuraterip_callback)callback context:(void *)context
{
    self = [super init];
    if (self) {
        _disc = *disc;
        _callback = callback;
        _context = context;
        _record = [[NSMutableData alloc] init];
        if (!_record) {
            [self release];
            return nil;
        }
    }
    return self;
}

- (void)dealloc
{
    [_record release];
    [_task release];
    [super dealloc];
}

- (BOOL)prepareTaskInSession:(NSURLSession *)session
{
    uint32_t id1 = _disc.accuraterip_id1;
    NSURL *url = [NSURL URLWithString:[NSString stringWithFormat:
        @"http://www.accuraterip.com/accuraterip/%x/%x/%x/dBAR-%03u-%08x-%08x-%08x.bin",
        id1 & 15u, (id1 >> 4) & 15u, (id1 >> 8) & 15u, _disc.accuraterip_tracks,
        id1, _disc.accuraterip_id2, _disc.accuraterip_cddb_id]];
    NSURLRequest *request = [NSURLRequest requestWithURL:url
        cachePolicy:NSURLRequestUseProtocolCachePolicy timeoutInterval:15.0];
    _task = [[session dataTaskWithRequest:request] retain];
    return _task != nil;
}

- (void)resume { [_task resume]; }
- (void)cancel { [_task cancel]; }

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task
    willPerformHTTPRedirection:(NSHTTPURLResponse *)response newRequest:(NSURLRequest *)request
             completionHandler:(void (^)(NSURLRequest *))completion
{
    (void)session;
    (void)task;
    NSURL *url = request.URL;
    BOOL https = [url.scheme isEqualToString:@"https"];
    BOOL http = [url.scheme isEqualToString:@"http"];
    BOOL allowedScheme = https || (http && [response.URL.scheme isEqualToString:@"http"]);
    if (allowedScheme && [url.host.lowercaseString isEqualToString:@"www.accuraterip.com"] &&
        (!url.port || url.port.intValue == (https ? 443 : 80)) && !url.user && !url.password) {
        completion(request);
    } else {
        _httpStatus = (int32_t)response.statusCode;
        _status = XLD_RIPPER_ACCURATERIP_NETWORK_ERROR;
        completion(nil);
    }
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task
 didReceiveResponse:(NSURLResponse *)response
  completionHandler:(void (^)(NSURLSessionResponseDisposition))completion
{
    (void)session;
    (void)task;
    _httpStatus = [response isKindOfClass:[NSHTTPURLResponse class]] ?
        (int32_t)[(NSHTTPURLResponse *)response statusCode] : 0;
    if (_httpStatus == 404) _status = XLD_RIPPER_ACCURATERIP_NOT_FOUND;
    else if (_httpStatus != 200) _status = XLD_RIPPER_ACCURATERIP_NETWORK_ERROR;
    else if (response.expectedContentLength > XLD_RIPPER_ACCURATERIP_MAX_BYTES)
        _status = XLD_RIPPER_INVALID_ACCURATERIP;
    completion(_status == XLD_RIPPER_OK ? NSURLSessionResponseAllow : NSURLSessionResponseCancel);
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task didReceiveData:(NSData *)data
{
    (void)session;
    if (_status != XLD_RIPPER_OK) return;
    if (data.length > XLD_RIPPER_ACCURATERIP_MAX_BYTES - _record.length) {
        _status = XLD_RIPPER_INVALID_ACCURATERIP;
        [task cancel];
        return;
    }
    [_record appendData:data];
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error
{
    (void)session;
    (void)task;
    if (_status == XLD_RIPPER_OK) {
        if ([error.domain isEqualToString:NSURLErrorDomain] && error.code == NSURLErrorCancelled)
            _status = XLD_RIPPER_CANCELLED;
        else if (error || _httpStatus != 200)
            _status = XLD_RIPPER_ACCURATERIP_NETWORK_ERROR;
        else if (!_record.length)
            _status = XLD_RIPPER_INVALID_ACCURATERIP;
        else
            _status = xld_ripper_accuraterip_data_validate(&_disc, _record.bytes, _record.length);
    }
    BOOL found = _status == XLD_RIPPER_OK;
    _callback(_status, _httpStatus, found ? _record.bytes : NULL, found ? _record.length : 0, _context);
    [_record release];
    _record = nil;
}
@end

xld_ripper_status xld_ripper_accuraterip_query_start(const xld_ripper_disc_info *disc,
                                                    xld_ripper_accuraterip_callback callback,
                                                    void *context, xld_ripper_accuraterip_query **out)
{
    if (!out) return XLD_RIPPER_INVALID_ARGUMENT;
    *out = NULL;
    if (!disc || !callback || disc->accuraterip_tracks > 99) return XLD_RIPPER_INVALID_ARGUMENT;
    if (!disc->accuraterip_tracks) return XLD_RIPPER_UNSUPPORTED;

    @autoreleasepool {
        XLDAccurateRipQuery *query = [[XLDAccurateRipQuery alloc] initWithDisc:disc
            callback:callback context:context];
        if (!query) return XLD_RIPPER_NO_MEMORY;
        NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        config.timeoutIntervalForResource = 15.0;
        config.HTTPShouldSetCookies = NO;
        config.HTTPCookieStorage = nil;
        config.URLCredentialStorage = nil;
        NSURLSession *session = [NSURLSession sessionWithConfiguration:config delegate:query delegateQueue:nil];
        if (![query prepareTaskInSession:session]) {
            [session invalidateAndCancel];
            [query release];
            return XLD_RIPPER_NO_MEMORY;
        }
        *out = (xld_ripper_accuraterip_query *)query;
        [query resume];
        [session finishTasksAndInvalidate];
    }
    return XLD_RIPPER_OK;
}

void xld_ripper_accuraterip_query_cancel(xld_ripper_accuraterip_query *query)
{
    [(XLDAccurateRipQuery *)query cancel];
}

void xld_ripper_accuraterip_query_free(xld_ripper_accuraterip_query *query)
{
    [(XLDAccurateRipQuery *)query release];
}
