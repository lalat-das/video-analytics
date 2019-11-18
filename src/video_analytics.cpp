// Copyright (c) 2019 Intel Corporation.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM,OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

/**
 * @brief VideoAnalytics Implementation
 */

#include "eis/va/video_analytics.h"

#define DEFAULT_QUEUE_SIZE 10

using namespace eis::va;
using namespace eis::utils;
using namespace eis::msgbus;

VideoAnalytics::VideoAnalytics(
        std::condition_variable& err_cv, EnvConfig* env_config, char* va_config) :
    m_err_cv(err_cv), m_enc_type(EncodeType::NONE), m_enc_lvl(0)
{
    // Parse the configuration
    config_t* config = json_config_new_from_buffer(va_config);
    if(config == NULL) {
        const char* err = "Failed to initialize configuration object";
        LOG_ERROR("%s", err);
        throw(err);
    }

    config_value_t* encoding_value = config->get_config_value(config->cfg,
                                                              "encoding");
    if(encoding_value == NULL) {
        const char* err = "\"encoding\" key is missing";
        LOG_WARN("%s", err);
    } else {
        config_value_t* encoding_type_cvt = config_value_object_get(encoding_value,
                                                                    "type");
        if(encoding_type_cvt == NULL) {
            const char* err = "encoding \"type\" key missing";
            LOG_ERROR("%s", err);
            config_destroy(config);
            throw(err);
        }
        if(encoding_type_cvt->type != CVT_STRING) {
            const char* err = "encoding \"type\" value has to be of string type";
            LOG_ERROR("%s", err);
            config_destroy(config);
            config_value_destroy(encoding_type_cvt);
            throw(err);
        }
        char* enc_type = encoding_type_cvt->body.string;
        if(strcmp(enc_type, "jpeg") == 0) {
            m_enc_type = EncodeType::JPEG;
        } else if(strcmp(enc_type, "png") == 0) {
            m_enc_type = EncodeType::PNG;
        }

        config_value_t* encoding_level_cvt = config_value_object_get(encoding_value,
                                                                    "level");
        if(encoding_level_cvt == NULL) {
            const char* err = "encoding \"level\" key missing";
            LOG_ERROR("%s", err);
            config_destroy(config);
            throw(err);
        }
        if(encoding_level_cvt->type != CVT_INTEGER) {
            const char* err = "encoding \"level\" value has to be of string type";
            LOG_ERROR("%s", err);
            config_destroy(config);
            config_value_destroy(encoding_level_cvt);
            throw(err);
        }
        m_enc_lvl = encoding_level_cvt->body.integer;
    }

    // Get queue size configuration
    config_value_t* queue_cvt = config->get_config_value(
            config->cfg, "queue_size");
    size_t queue_size = DEFAULT_QUEUE_SIZE;
    if(queue_cvt == NULL) {
        LOG_INFO("\"queue_size\" key missing, using default queue size: \
                %ld", queue_size);
    } else {
        if(queue_cvt->type != CVT_INTEGER) {
            const char* err = "\"queue_size\" value has to be of integer type";
            LOG_ERROR("%s", err);
            config_destroy(config);
            config_value_destroy(queue_cvt);
            throw(err);
        }

        queue_size = queue_cvt->body.integer;
    }

    // Get configuration values for the subscriber
    LOG_DEBUG_0("Parsing VA subscription topics");
    std::vector<std::string> sub_topics = env_config->get_topics_from_env("sub");
    if(sub_topics.size() != 1) {
        const char* err = "Only one topic is supported. Neither more nor less";
        LOG_ERROR("%s", err);
        config_destroy(config);
        throw(err);
    }

    std::string topic_type_sub = "sub";
    config_t* msgbus_config_sub = env_config->get_messagebus_config(
            sub_topics[0], topic_type_sub);
    if(msgbus_config_sub == NULL) {
        const char* err = "Failed to get subscriber message bus config";
        LOG_ERROR("%s", err);
        config_destroy(config);
        throw(err);
    }

    // Get configuration values for the publisher
    LOG_DEBUG_0("Parsing VA publisher topics");
    std::vector<std::string> topics = env_config->get_topics_from_env("pub");
    if(topics.size() != 1) {
        const char* err = "Only one topic is supported. Neither more nor less";
        LOG_ERROR("%s", err);
        config_destroy(config);
        throw(err);
    }

    std::string topic_type = "pub";
    config_t* pub_config = env_config->get_messagebus_config(
            topics[0], topic_type);
    if(pub_config == NULL) {
        const char* err = "Failed to get publisher message bus config";
        LOG_ERROR("%s", err);
        config_destroy(config);
        throw(err);
    }

    // Initialize input and output queues
    m_udf_input_queue = new FrameQueue(queue_size);
    m_udf_output_queue = new FrameQueue(queue_size);

    // Initialize Publisher
    m_publisher = new Publisher(
            pub_config, m_err_cv, topics[0],
            (MessageQueue*) m_udf_output_queue);

    // Initialize UDF Manager
    m_udf_manager = new UdfManager(
            config, m_udf_input_queue, m_udf_output_queue);

    // Initialize subscriber
    m_subscriber = new Subscriber<eis::udf::Frame>(
            msgbus_config_sub, m_err_cv, sub_topics[0],
            (MessageQueue*) m_udf_input_queue);
}

void VideoAnalytics::start() {
    m_publisher->start();
    LOG_INFO_0("Publisher thread started...");

    m_udf_manager->start();
    LOG_INFO_0("Started udf manager");

    m_subscriber->start();
    LOG_INFO_0("Subscriber thread started...");
}

void VideoAnalytics::stop() {
    m_subscriber->stop();
    m_udf_manager->stop();
    m_publisher->stop();
}

VideoAnalytics::~VideoAnalytics() {
    // NOTE: stop() methods are automatically called in all destructors
    delete m_subscriber;
    delete m_udf_manager;
    delete m_publisher;
}