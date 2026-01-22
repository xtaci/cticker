/*
MIT License

Copyright (c) 2026 xtaci

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/**
 * @file api.c
 * @brief Networking + JSON parsing for Binance endpoints.
 *
 * This module provides two high-level calls:
 * - fetch_ticker_data(): latest price + 24h change for a symbol
 * - fetch_historical_data(): OHLC candles for charting
 *
 * Ownership:
 * - fetch_ticker_data() fills a caller-provided ::TickerData.
 * - fetch_historical_data() allocates @p *points; caller must free(@p *points).
 */

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

/**
 * @brief In-memory buffer for the HTTP response body.
 *
 * The libcurl write callback appends bytes into (data,size) using realloc().
 */
typedef struct {
    char *data;
    size_t size;
} ResponseBuffer;

/**
 * @brief libcurl write callback.
 * @return Number of bytes consumed; returning 0 tells libcurl to abort.
 */
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

/**
 * @brief Fetch latest ticker data from Binance.
 *
 * Endpoint returns a JSON object with fields like:
 * - lastPrice
 * - priceChangePercent
 */
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
    /* Keep UI responsive even on slow networks. */
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        free(response.data);
        return -1;
    }
    
    /* Parse JSON response (expected to be an object). */
    json_error_t error;
    json_t *root = json_loads(response.data, 0, &error);
    free(response.data);
    
    if (!root) {
        return -1;
    }
    
    /* Extract data into the caller-owned output struct. */
    snprintf(data->symbol, sizeof(data->symbol), "%s", symbol);
    data->price = 0.0;
    data->change_24h = 0.0;
    data->high_price = 0.0;
    data->low_price = 0.0;
    data->volume_base = 0.0;
    data->volume_quote = 0.0;
    data->trade_count = 0;
    data->price_text[0] = '\0';
    data->high_text[0] = '\0';
    data->low_text[0] = '\0';
    
    json_t *price_json = json_object_get(root, "lastPrice");
    json_t *change_json = json_object_get(root, "priceChangePercent");
    json_t *high_json = json_object_get(root, "highPrice");
    json_t *low_json = json_object_get(root, "lowPrice");
    json_t *volume_json = json_object_get(root, "volume");
    json_t *quote_volume_json = json_object_get(root, "quoteVolume");
    json_t *trade_count_json = json_object_get(root, "count");
    
    if (json_is_string(price_json)) {
        const char *price_str = json_string_value(price_json);
        data->price = atof(price_str);
        snprintf(data->price_text, sizeof(data->price_text), "%s", price_str);
    }

    if (data->price_text[0] == '\0') {
        snprintf(data->price_text, sizeof(data->price_text), "%.8f", data->price);
    }
    
    if (json_is_string(change_json)) {
        data->change_24h = atof(json_string_value(change_json));
    }

    if (json_is_string(high_json)) {
        const char *high_str = json_string_value(high_json);
        data->high_price = atof(high_str);
        snprintf(data->high_text, sizeof(data->high_text), "%s", high_str);
    } else {
        data->high_price = data->price;
    }

    if (data->high_text[0] == '\0') {
        snprintf(data->high_text, sizeof(data->high_text), "%.8f", data->high_price);
    }

    if (json_is_string(low_json)) {
        const char *low_str = json_string_value(low_json);
        data->low_price = atof(low_str);
        snprintf(data->low_text, sizeof(data->low_text), "%s", low_str);
    } else {
        data->low_price = data->price;
    }

    if (data->low_text[0] == '\0') {
        snprintf(data->low_text, sizeof(data->low_text), "%.8f", data->low_price);
    }

    if (json_is_string(volume_json)) {
        data->volume_base = atof(json_string_value(volume_json));
    } else {
        data->volume_base = 0.0;
    }

    if (json_is_string(quote_volume_json)) {
        data->volume_quote = atof(json_string_value(quote_volume_json));
    } else {
        data->volume_quote = 0.0;
    }

    if (json_is_integer(trade_count_json)) {
        data->trade_count = (int)json_integer_value(trade_count_json);
    } else {
        data->trade_count = 0;
    }
    
    data->timestamp = time(NULL);
    
    json_decref(root);
    return 0;
}

/**
 * @brief Convert UI period selection into Binance kline interval + request limit.
 *
 * Limit is chosen to keep charts informative while avoiding overly large
 * responses (also keeps rendering and parsing fast).
 */
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

/**
 * @brief Fetch historical kline data from Binance.
 *
 * The response is a JSON array of arrays. For each kline we read:
 * - [0] open time (ms)
 * - [1] open
 * - [2] high
 * - [3] low
 * - [4] close
 * - [5] volume
 * - [6] close time (ms)
 * - [7] quote asset volume
 * - [8] number of trades
 * - [9] taker buy base asset volume
 * - [10] taker buy quote asset volume
 */
int fetch_historical_data(const char *symbol, Period period,
                          PricePoint **points, int *count) {
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
    
    /* Parse JSON response (expected to be an array). */
    json_error_t error;
    json_t *root = json_loads(response.data, 0, &error);
    free(response.data);
    
    if (!root || !json_is_array(root)) {
        if (root) json_decref(root);
        return -1;
    }
    
    size_t array_size = json_array_size(root);
    /*
     * Allocate the worst-case number of points; we may end up using fewer if
     * some elements are malformed.
     */
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
        json_t *volume = json_array_get(kline, 5);
        json_t *close_time = json_array_get(kline, 6);
        json_t *quote_volume = json_array_get(kline, 7);
        json_t *trade_count = json_array_get(kline, 8);
        json_t *taker_buy_base = json_array_get(kline, 9);
        json_t *taker_buy_quote = json_array_get(kline, 10);
        
        if (json_is_integer(timestamp) && json_is_string(open_price) &&
            json_is_string(high_price) && json_is_string(low_price) &&
            json_is_string(close_price) && json_is_string(volume) &&
            json_is_integer(close_time) && json_is_string(quote_volume) &&
            json_is_integer(trade_count) && json_is_string(taker_buy_base) &&
            json_is_string(taker_buy_quote)) {
            PricePoint *point = &(*points)[*count];
            /* Binance timestamps are milliseconds; we store seconds. */
            point->timestamp = json_integer_value(timestamp) / 1000;
            point->close_time = json_integer_value(close_time) / 1000;
            point->open = atof(json_string_value(open_price));
            snprintf(point->open_text, sizeof(point->open_text), "%s", json_string_value(open_price));
            point->high = atof(json_string_value(high_price));
            snprintf(point->high_text, sizeof(point->high_text), "%s", json_string_value(high_price));
            point->low = atof(json_string_value(low_price));
            snprintf(point->low_text, sizeof(point->low_text), "%s", json_string_value(low_price));
            point->close = atof(json_string_value(close_price));
            snprintf(point->close_text, sizeof(point->close_text), "%s", json_string_value(close_price));
            point->volume = atof(json_string_value(volume));
            point->quote_volume = atof(json_string_value(quote_volume));
            point->trade_count = (int)json_integer_value(trade_count);
            point->taker_buy_base_volume = atof(json_string_value(taker_buy_base));
            point->taker_buy_quote_volume = atof(json_string_value(taker_buy_quote));
            (*count)++;
        }
    }
    
    json_decref(root);
    return 0;
}
