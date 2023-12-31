// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

package org.apache.doris.planner.external;

import org.apache.doris.analysis.Expr;
import org.apache.doris.analysis.TupleDescriptor;
import org.apache.doris.catalog.external.HMSExternalTable;
import org.apache.doris.common.DdlException;
import org.apache.doris.common.MetaNotFoundException;
import org.apache.doris.common.UserException;
import org.apache.doris.planner.ColumnRange;
import org.apache.doris.thrift.TFileFormatType;

import com.google.common.collect.Lists;
import org.apache.hadoop.mapred.FileSplit;
import org.apache.hadoop.mapred.InputSplit;

import java.util.Collections;
import java.util.List;
import java.util.Map;

/**
 * A file scan provider for hudi.
 * HudiProvier is extended with hive since they both use input format interface to get the split.
 */
public class HudiScanProvider extends HiveScanProvider {

    public HudiScanProvider(HMSExternalTable hmsTable, TupleDescriptor desc,
            Map<String, ColumnRange> columnNameToRange) {
        super(hmsTable, desc, columnNameToRange);
    }

    @Override
    public TFileFormatType getFileFormatType() throws DdlException {
        return TFileFormatType.FORMAT_PARQUET;
    }

    @Override
    public List<String> getPathPartitionKeys() throws DdlException, MetaNotFoundException {
        return Collections.emptyList();
    }

    @Override
    public List<InputSplit> getSplits(List<Expr> exprs) throws UserException {
        List<InputSplit> splits = super.getSplits(exprs);
        List<InputSplit> filterSplits = Lists.newArrayList();
        for (InputSplit split : splits) {
            FileSplit fileSplit = (FileSplit) split;
            // skip hidden files start with "."
            if (fileSplit.getPath().getName().startsWith(".")) {
                continue;
            }
            filterSplits.add(split);
        }
        return filterSplits;
    }
}
