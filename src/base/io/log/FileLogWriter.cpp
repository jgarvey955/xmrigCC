/* XMRig
 * Copyright (c) 2018-2021 SChernykh   <https://github.com/SChernykh>
 * Copyright (c) 2016-2021 XMRig       <https://github.com/xmrig>, <support@xmrig.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include "base/io/log/FileLogWriter.h"
#include "base/io/Env.h"


#include <cassert>


namespace xmrig {


static void fsWriteCallback(uv_fs_t *req)
{
    delete [] static_cast<char *>(req->data);

    uv_fs_req_cleanup(req);
    delete req;
}


} // namespace xmrig


xmrig::FileLogWriter::FileLogWriter()
{
    init();
}

xmrig::FileLogWriter::FileLogWriter(const char* fileName)
{
    init();
    open(fileName);
}

xmrig::FileLogWriter::~FileLogWriter()
{
    uv_close(reinterpret_cast<uv_handle_t*>(&m_flushAsync), nullptr);

    uv_mutex_destroy(&m_buffersLock);
}

void xmrig::FileLogWriter::init()
{
    uv_mutex_init(&m_buffersLock);

    uv_async_init(uv_default_loop(), &m_flushAsync, on_flush);
    m_flushAsync.data = this;
}

bool xmrig::FileLogWriter::open(const char *fileName)
{
    assert(fileName != nullptr);
    if (!fileName) {
        return false;
    }

    uv_fs_t req{};
    const auto path = Env::expand(fileName);
    m_file          = uv_fs_open(uv_default_loop(), &req, path, O_CREAT | O_WRONLY, 0644, nullptr);

    if (req.result < 0 || !isOpen()) {
        uv_fs_req_cleanup(&req);
        m_file = -1;

        return false;
    }

    uv_fs_req_cleanup(&req);

    uv_fs_stat(uv_default_loop(), &req, path, nullptr);
    m_pos = req.statbuf.st_size;
    uv_fs_req_cleanup(&req);

    return true;
}


bool xmrig::FileLogWriter::write(const char *data, size_t size)
{
    if (!isOpen()) {
        return false;
    }

    uv_buf_t buf = uv_buf_init(new char[size], static_cast<unsigned int>(size));
    memcpy(buf.base, data, size);

    return enqueue(buf);
}


bool xmrig::FileLogWriter::writeLine(const char *data, size_t size)
{
    if (!isOpen()) {
        return false;
    }

    constexpr size_t N = sizeof(m_endl) - 1;

    uv_buf_t buf = uv_buf_init(new char[size + N], static_cast<unsigned int>(size + N));
    memcpy(buf.base, data, size);
    memcpy(buf.base + size, m_endl, N);

    return enqueue(buf);
}


bool xmrig::FileLogWriter::enqueue(uv_buf_t buf)
{
    uv_mutex_lock(&m_buffersLock);
    if (buf.len > kMaxBufferedBytes || m_bufferedBytes > kMaxBufferedBytes - buf.len) {
        uv_mutex_unlock(&m_buffersLock);
        delete [] buf.base;
        return false;
    }

    m_buffers.emplace_back(buf);
    m_bufferedBytes += buf.len;
    uv_async_send(&m_flushAsync);
    uv_mutex_unlock(&m_buffersLock);

    return true;
}

void xmrig::FileLogWriter::flush()
{
    std::vector<uv_buf_t> buffers;
    uv_mutex_lock(&m_buffersLock);
    buffers.swap(m_buffers);
    m_bufferedBytes = 0;
    uv_mutex_unlock(&m_buffersLock);

    if (buffers.empty()) {
        return;
    }

    size_t size = 0;
    for (const auto &buf : buffers) {
        size += buf.len;
    }

    char *data = new char[size];
    size_t offset = 0;
    for (const auto &buf : buffers) {
        memcpy(data + offset, buf.base, buf.len);
        offset += buf.len;
        delete [] buf.base;
    }

    uv_buf_t output = uv_buf_init(data, static_cast<unsigned int>(size));
    uv_fs_t *req = new uv_fs_t;
    req->data = data;

    const int rc = uv_fs_write(uv_default_loop(), req, m_file, &output, 1, m_pos, fsWriteCallback);
    if (rc < 0) {
        delete [] data;
        uv_fs_req_cleanup(req);
        delete req;
        return;
    }

    m_pos += size;
}
