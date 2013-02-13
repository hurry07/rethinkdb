// Copyright 2010-2013 RethinkDB, all rights reserved.
#include "unittest/gtest.hpp"

#include "arch/io/disk.hpp"
#include "btree/btree_store.hpp"
#include "buffer_cache/mirrored/config.hpp"
#include "mock/unittest_utils.hpp"
#include "rdb_protocol/btree.hpp"
#include "rdb_protocol/proto_utils.hpp"
#include "serializer/log/log_serializer.hpp"

namespace unittest {

void run_sindex_post_construction() {
    mock::temp_file_t temp_file("/tmp/rdb_unittest.XXXXXX");

    scoped_ptr_t<io_backender_t> io_backender;
    make_io_backender(aio_default, &io_backender);

    filepath_file_opener_t file_opener(temp_file.name(), io_backender.get());
    standard_serializer_t::create(
        &file_opener,
        standard_serializer_t::static_config_t());

    standard_serializer_t serializer(
        standard_serializer_t::dynamic_config_t(),
        &file_opener,
        &get_global_perfmon_collection());

    rdb_protocol_t::store_t store(
            &serializer,
            "unit_test_store",
            GIGABYTE,
            true,
            &get_global_perfmon_collection(),
            NULL);

    cond_t dummy_interuptor;

    for (int i = 0; i < 10; ++i) {
        scoped_ptr_t<transaction_t> txn;
        scoped_ptr_t<real_superblock_t> superblock;
        write_token_pair_t token_pair;
        store.new_write_token_pair(&token_pair);
        store.acquire_superblock_for_write(rwi_write, repli_timestamp_t::invalid,
                                           1, &token_pair.main_write_token, &txn, &superblock, &dummy_interuptor);

        std::string data = strprintf("{\"id\" : %d, \"sid\" : %d}", i, i * i);
        point_write_response_t response;
        rdb_modification_report_t mod_report;
        rdb_set(store_key_t(cJSON_print_primary(scoped_cJSON_t(cJSON_CreateNumber(i)).get(), backtrace_t())),
                boost::shared_ptr<scoped_cJSON_t>(new scoped_cJSON_t(cJSON_Parse(data.c_str()))),
                false, store.btree.get(), repli_timestamp_t::invalid, txn.get(),
                superblock.get(), &response, &mod_report);
    }

    {
        uuid_u id = generate_uuid();
        write_token_pair_t token_pair;
        store.new_write_token_pair(&token_pair);

        scoped_ptr_t<transaction_t> txn;
        scoped_ptr_t<real_superblock_t> super_block;

        store.acquire_superblock_for_write(rwi_write, repli_timestamp_t::invalid,
                1, &token_pair.main_write_token, &txn, &super_block, &dummy_interuptor);

        Mapping m;
        *m.mutable_arg() = "row";
        m.mutable_body()->set_type(Term::CALL);
        *m.mutable_body()->mutable_call() = Term::Call();
        m.mutable_body()->mutable_call()->mutable_builtin()->set_type(Builtin::GETATTR);
        *m.mutable_body()->mutable_call()->mutable_builtin()->mutable_attr() = "sid";

        Term *arg = m.mutable_body()->mutable_call()->add_args();
        arg->set_type(Term::VAR);
        *arg->mutable_var() = "row";

        write_message_t wm;
        wm << m;

        vector_stream_t stream;
        int res = send_write_message(&stream, &wm);
        guarantee(res == 0);

        store.add_sindex(
                &token_pair,
                id,
                stream.vector(),
                txn.get(),
                super_block.get(),
                &dummy_interuptor);
    }

    {
        write_token_pair_t token_pair;
        store.new_write_token_pair(&token_pair);

        scoped_ptr_t<transaction_t> txn;
        scoped_ptr_t<real_superblock_t> super_block;
        store.acquire_superblock_for_write(rwi_write, repli_timestamp_t::invalid,
                1, &token_pair.main_write_token, &txn, &super_block, &dummy_interuptor);

        btree_store_t<rdb_protocol_t>::sindex_access_vector_t sindexes;
        store.acquire_all_sindex_superblocks_for_write(super_block->get_sindex_block_id(),
                &token_pair, txn.get(), &sindexes, &dummy_interuptor);

        post_construct_secondary_indexes(store.btree.get(), txn.get(), super_block.get(),
                sindexes, &dummy_interuptor);
    }
}

TEST(RDBBtree, SindexPostConstruct) {
    mock::run_in_thread_pool(&run_sindex_post_construction);
}

} //namespace unittest