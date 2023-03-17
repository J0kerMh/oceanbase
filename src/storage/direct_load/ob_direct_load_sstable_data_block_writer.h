// Copyright (c) 2022-present Oceanbase Inc. All Rights Reserved.
// Author:
//   suzhi.yt <>

#pragma once

#include "storage/direct_load/ob_direct_load_data_block_writer.h"
#include "storage/direct_load/ob_direct_load_sstable_data_block.h"

namespace oceanbase
{
namespace storage
{

template <typename T>
class ObDirectLoadSSTableDataBlockWriter
  : public ObDirectLoadDataBlockWriter<ObDirectLoadSSTableDataBlock::Header, T>
{
public:
  ObDirectLoadSSTableDataBlockWriter();
  virtual ~ObDirectLoadSSTableDataBlockWriter();
  int append_row(const T &row);
private:
  int pre_write_item() override;
  int pre_flush_buffer() override;
private:
  int32_t last_row_pos_;
  int32_t cur_row_pos_; // for one row data block
  DISALLOW_COPY_AND_ASSIGN(ObDirectLoadSSTableDataBlockWriter);
};

template <typename T>
ObDirectLoadSSTableDataBlockWriter<T>::ObDirectLoadSSTableDataBlockWriter()
  : last_row_pos_(-1), cur_row_pos_(-1)
{
}

template <typename T>
ObDirectLoadSSTableDataBlockWriter<T>::~ObDirectLoadSSTableDataBlockWriter()
{
}

template <typename T>
int ObDirectLoadSSTableDataBlockWriter<T>::append_row(const T &row)
{
  int ret = common::OB_SUCCESS;
  if (OB_FAIL(this->write_item(row))) {
    STORAGE_LOG(WARN, "fail to write item", KR(ret));
  } else {
    last_row_pos_ = cur_row_pos_;
  }
  return ret;
}

template <typename T>
int ObDirectLoadSSTableDataBlockWriter<T>::pre_write_item()
{
  cur_row_pos_ = this->data_block_writer_.get_pos();
  return common::OB_SUCCESS;
}

template <typename T>
int ObDirectLoadSSTableDataBlockWriter<T>::pre_flush_buffer()
{
  ObDirectLoadSSTableDataBlock::Header &header = this->data_block_writer_.get_header();
  header.last_row_pos_ = (last_row_pos_ != -1 ? last_row_pos_ : cur_row_pos_);
  last_row_pos_ = -1;
  cur_row_pos_ = -1;
  return common::OB_SUCCESS;
}

} // namespace storage
} // namespace oceanbase
