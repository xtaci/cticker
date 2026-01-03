#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <jansson.h>
#include <time.h>
#include "cticker.h"

#define BINANCE_API_BASE "https://api.binance.com"
#define BINANCE_TICKER_URL BINANCE_API_BASE "/api/v3/ticker/24hr?symbol=%s"
#define BINANCE_KLINES_URL BINANCE_API_BASE "/api/v3/klines?symbol=%s&interval=%s&limit=%d"

// Buffer for HTTP response
typedef struct {
    char *data;
    size_t size;
} ResponseBuffer;

// Callback for curl write
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    ResponseBuffer *mem = (ResponseBuffer *)userp;
    
    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) {
        return 0;
    }
    
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    
    return realsize;
}

// Fetch ticker data from Binance API
int fetch_ticker_data(const char *symbol, TickerData *data) {
    CURL *curl;
    CURLcode res;
    char url[512];
    ResponseBuffer response = {0};
    
    snprintf(url, sizeof(url), BINANCE_TICKER_URL, symbol);
    
    curl = curl_easy_init();
    if (!curl) {
        return -1;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        free(response.data);
        return -1;
    }
    
    // Parse JSON response
    json_error_t error;
    json_t *root = json_loads(response.data, 0, &error);
    free(response.data);
    
    if (!root) {
        return -1;
    }
    
    // Extract data
    strcpy(data->symbol, symbol);
    
    json_t *price_json = json_object_get(root, "lastPrice");
    json_t *change_json = json_object_get(root, "priceChangePercent");
    
    if (json_is_string(price_json)) {
        data->price = atof(json_string_value(price_json));
    }
    
    if (json_is_string(change_json)) {
        data->change_24h = atof(json_string_value(change_json));
    }
    
    data->timestamp = time(NULL);
    
    json_decref(root);
    return 0;
}

// Get interval string and limit for period
static void get_interval_params(Period period, const char **interval, int *limit) {
    switch (period) {
        case PERIOD_1MIN:
            *interval = "1m";
            *limit = 240;  // 4 hours of 1-minute candles
            break;
        case PERIOD_15MIN:
            *interval = "15m";
            *limit = 192;  // 2 days of 15-minute candles
            break;
        case PERIOD_1HOUR:
            *interval = "1h";
            *limit = 168;  // 1 week of hourly candles
            break;
        case PERIOD_4HOUR:
            *interval = "4h";
            *limit = 180;  // ~30 days of 4-hour candles
            break;
        case PERIOD_1DAY:
            *interval = "1d";
            *limit = 120;  // ~4 months of daily candles
            break;
        case PERIOD_1WEEK:
            *interval = "1w";
            *limit = 104;  // 2 years of weekly candles
            break;
        case PERIOD_1MONTH:
        default:
            *interval = "1M";
            *limit = 120;  // 10 years of monthly candles
            break;
    }
}

// Fetch historical data from Binance API
int fetch_historical_data(const char *symbol, Period period, PricePoint **points, int *count) {
    CURL *curl;
    CURLcode res;
    char url[512];
    ResponseBuffer response = {0};
    const char *interval = "15m";
    int limit = 96;
    
    get_interval_params(period, &interval, &limit);
    snprintf(url, sizeof(url), BINANCE_KLINES_URL, symbol, interval, limit);
    
    curl = curl_easy_init();
    if (!curl) {
        return -1;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        free(response.data);
        return -1;
    }
    
    // Parse JSON response
    json_error_t error;
    json_t *root = json_loads(response.data, 0, &error);
    free(response.data);
    
    if (!root || !json_is_array(root)) {
        if (root) json_decref(root);
        return -1;
    }
    
    size_t array_size = json_array_size(root);
    *points = malloc(sizeof(PricePoint) * array_size);
    if (!*points) {
        json_decref(root);
        return -1;
    }
    
    *count = 0;
    for (size_t i = 0; i < array_size; i++) {
        json_t *kline = json_array_get(root, i);
        if (!json_is_array(kline)) continue;
        
        json_t *timestamp = json_array_get(kline, 0);
        json_t *open_price = json_array_get(kline, 1);
        json_t *high_price = json_array_get(kline, 2);
        json_t *low_price = json_array_get(kline, 3);
        json_t *close_price = json_array_get(kline, 4);
        
        if (json_is_integer(timestamp) && json_is_string(open_price) &&
            json_is_string(high_price) && json_is_string(low_price) &&
            json_is_string(close_price)) {
            PricePoint *point = &(*points)[*count];
            point->timestamp = json_integer_value(timestamp) / 1000;
            point->open = atof(json_string_value(open_price));
            point->high = atof(json_string_value(high_price));
            point->low = atof(json_string_value(low_price));
            point->close = atof(json_string_value(close_price));
            (*count)++;
        }
    }
    
    json_decref(root);
    return 0;
}
