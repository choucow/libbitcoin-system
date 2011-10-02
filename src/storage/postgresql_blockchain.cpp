#include "postgresql_blockchain.hpp"

#include <bitcoin/constants.hpp>
#include <bitcoin/dialect.hpp>
#include <bitcoin/transaction.hpp>
#include <bitcoin/util/assert.hpp>
#include <bitcoin/util/logger.hpp>

namespace libbitcoin {

postgresql_organizer::postgresql_organizer(cppdb::session sql)
  : sql_(sql)
{
}

void postgresql_organizer::delete_chains(size_t left, size_t right)
{
    static cppdb::statement delete_chains = sql_.prepare(
        "DELETE FROM chains \
        WHERE chain_id BETWEEN ? AND ?"
        );
    delete_chains.reset();
    delete_chains.bind(left);
    delete_chains.bind(right);
    delete_chains.exec();

    size_t offset = (right + 1) - left;

    static cppdb::statement adjust_chains = sql_.prepare(
        "UPDATE chains \
        SET chain_id = chain_id - ? \
        WHERE chain_id > ?"
        );
    adjust_chains.reset();
    adjust_chains.bind(offset);
    adjust_chains.bind(right);
    adjust_chains.exec();
}

void postgresql_organizer::unwind_chain(size_t depth, size_t chain_id)
{
    static cppdb::statement unwind_chain = sql_.prepare(
        "UPDATE chains \
        SET work = work - \
            (SELECT SUM(difficulty(bits_head, bits_body)) \
            FROM blocks \
            WHERE \
                space=0 \
                AND depth >= ? \
                AND span_left <= ? \
                AND span_right >= ? \
                AND status='valid') \
        WHERE chain_id=?"
        );
    unwind_chain.reset();
    unwind_chain.bind(depth);
    unwind_chain.bind(chain_id);
    unwind_chain.bind(chain_id);
    unwind_chain.bind(chain_id);
    unwind_chain.exec();
}

void postgresql_organizer::delete_branch(size_t space, size_t depth, 
    size_t span_left, size_t span_right)
{
    static cppdb::statement lonely_child = sql_.prepare(
        "SELECT 1 \
        FROM blocks \
        WHERE \
            space = ? \
            AND depth = ? - 1 \
            AND span_left = ? \
            AND span_right = ? \
        LIMIT 1"
        );
    lonely_child.reset();
    lonely_child.bind(space);
    lonely_child.bind(depth);
    lonely_child.bind(span_left);
    lonely_child.bind(span_right);

    size_t offset = span_right - span_left;

    if (lonely_child.row().empty())
    {
        offset++;
        delete_chains(span_left, span_right);
    }
    else
    {
        delete_chains(span_left + 1, span_right);
        unwind_chain(depth, span_left);
    }
    
    static cppdb::statement delete_branch = sql_.prepare(
        "DELETE FROM blocks \
        WHERE \
            space=? \
            AND depth >= ? \
            AND span_left >= ? \
            AND span_right <= ?"
        );
    delete_branch.reset();
    delete_branch.bind(space);
    delete_branch.bind(depth);
    delete_branch.bind(span_left);
    delete_branch.bind(span_right);
    delete_branch.exec();

    static cppdb::statement adjust_left = sql_.prepare(
        "UPDATE blocks \
        SET span_left = span_left - ? \
        WHERE  \
            space = ? \
            AND span_left > ?"
        );
    adjust_left.reset();
    adjust_left.bind(offset);
    adjust_left.bind(space);
    adjust_left.bind(span_right);
    adjust_left.exec();

    static cppdb::statement adjust_right = sql_.prepare(
        "UPDATE blocks \
        SET span_right = span_right - ? \
        WHERE  \
            space = ? \
            AND span_right >= ?"
        );
    adjust_right.reset();
    adjust_right.bind(offset);
    adjust_right.bind(space);
    adjust_right.bind(span_right);
    adjust_right.exec();
}

void postgresql_organizer::organize()
{
    static cppdb::statement orphans_statement = sql_.prepare(
        "SELECT \
            block.block_id, \
            block.space, \
            block.depth, \
            parent.block_id \
        FROM \
            blocks block, \
            blocks parent \
        WHERE \
            block.prev_block_hash=parent.block_hash \
            AND block.space > 0 \
            AND block.depth=0"
        );
    orphans_statement.reset();
    cppdb::result orphans_results = orphans_statement.query();
    while (orphans_results.next())
    {
        size_t child_id = orphans_results.get<size_t>(0),
            child_space = orphans_results.get<size_t>(1),
            child_depth = orphans_results.get<size_t>(2),
            parent_id = orphans_results.get<size_t>(3);
        BITCOIN_ASSERT(child_depth == 0);

        static cppdb::statement point_prev_statement = sql_.prepare(
            "UPDATE blocks \
            SET prev_block_id=? \
            WHERE block_id=?"
            );
        point_prev_statement.reset();
        point_prev_statement.bind(parent_id);
        point_prev_statement.bind(child_id);
        point_prev_statement.exec();

        size_t parent_space, parent_depth;
        span parent_span;
        // Parent depth and space can change if it is
        // joined to another branch
        if (!load_position_info(parent_id, 
                parent_space, parent_depth, parent_span))
        {
            // Something went very wrong. Stop.
            return;
        }

        // During this loop the span can be modified so we can't load it above
        span child_span;
        if (!load_span(child_id, child_span))
            return;
        BITCOIN_ASSERT(child_span.left == 0);

        size_t parent_width = 
            get_block_width(parent_space, parent_depth, parent_span);
        size_t child_width = child_span.right - child_span.left + 1;

        size_t new_child_span_left = parent_span.right;
        if (parent_width > 0)
            new_child_span_left++;

        size_t new_child_depth = parent_depth + 1;
        reserve_branch_area(parent_space, parent_width, parent_span, 
            new_child_depth, child_width);
        position_child_branch(child_space, parent_space, new_child_depth, 
            new_child_span_left);
    }
}

bool postgresql_organizer::load_span(size_t block_id, span& spn)
{
    static cppdb::statement statement = sql_.prepare(
        "SELECT \
            span_left, \
            span_right \
        FROM blocks \
        WHERE block_id=?"
        );
    statement.reset();
    statement.bind(block_id);
    cppdb::result result = statement.row();
    if (result.empty())
    {
        log_fatal() << "load_span() failed for block " << block_id;
        return false;
    }
    spn.left = result.get<size_t>(0);
    spn.right = result.get<size_t>(1);
    BITCOIN_ASSERT(spn.left <= spn.right);
    return true;
}

bool postgresql_organizer::load_position_info(size_t block_id, 
    size_t& space, size_t& depth, span& spn)
{
    static cppdb::statement statement = sql_.prepare(
        "SELECT \
            space, \
            depth, \
            span_left, \
            span_right \
        FROM blocks \
        WHERE block_id=?"
        );
    statement.reset();
    statement.bind(block_id);
    cppdb::result result = statement.row();
    if (result.empty())
    {
        log_fatal() << "load_parent() failed for block " << block_id;
        return false;
    }
    space = result.get<size_t>(0);
    depth = result.get<size_t>(1);
    spn.left = result.get<size_t>(2);
    spn.right = result.get<size_t>(3);
    BITCOIN_ASSERT(spn.left <= spn.right);
    return true;
}

size_t postgresql_organizer::get_block_width(
        size_t space, size_t depth, span block_span)
{
    // If parent's span_left < span_right then parent has children
    if (block_span.left < block_span.right)
        return block_span.right - block_span.left + 1;
    static cppdb::statement statement = sql_.prepare(
        "SELECT 1 \
        FROM blocks \
        WHERE \
            space=? \
            AND depth > ? \
            AND span_left >= ? \
            AND span_right <= ? \
        LIMIT 1"
        );
    statement.reset();
    statement.bind(space);
    statement.bind(depth);
    statement.bind(block_span.left);
    statement.bind(block_span.right);
    cppdb::result has_children_result = statement.row();
    if (has_children_result.empty())
    {
        BITCOIN_ASSERT(block_span.left == block_span.left);
        return 0;
    }
    return 1;
}

void postgresql_organizer::reserve_branch_area(size_t parent_space, 
    size_t parent_width, const span& parent_span, 
        size_t new_child_depth, size_t child_width)
{
    if (parent_width == 0 && child_width == 1)
        // Do nothing
        return;

    // Shift everything to the right
    static cppdb::statement update_right = sql_.prepare(
        "UPDATE blocks \
        SET span_right = span_right + ? \
        WHERE \
            space=? \
            AND span_right > ?"
        );
    update_right.reset();
    update_right.bind(child_width);
    update_right.bind(parent_space);
    update_right.bind(parent_span.right);
    update_right.exec();

    static cppdb::statement update_left = sql_.prepare(
        "UPDATE blocks \
        SET span_left = span_left + ? \
        WHERE \
            space=? \
            AND span_left > ?"
        );
    update_left.reset();
    update_left.bind(child_width);
    update_left.bind(parent_space);
    update_left.bind(parent_span.right);
    update_left.exec();

    // Expand parent's right bracket
    static cppdb::statement update_parents = sql_.prepare(
        "UPDATE blocks \
        SET span_right = span_right + ? \
        WHERE \
            space=? \
            AND depth < ? \
            AND span_right=?"
        );
    update_parents.reset();
    update_parents.bind(child_width);
    update_parents.bind(parent_space);
    update_parents.bind(new_child_depth);
    update_parents.bind(parent_span.right);
    update_parents.exec();

    // Chains only apply to space 0
    if (parent_space != 0)
        return;

    // Fix chain info
    static cppdb::statement update_other_chains = sql_.prepare(
        "UPDATE chains \
        SET chain_id = chain_id + ? \
        WHERE chain_id > ?"
        );
    update_other_chains.reset();
    update_other_chains.bind(child_width);
    update_other_chains.bind(parent_span.right);
    update_other_chains.exec();

    static cppdb::statement tween_chains = sql_.prepare(
        "INSERT INTO chains ( \
            work, \
            chain_id, \
            depth \
        ) SELECT \
            work, \
            chain_id + ?, \
            depth \
        FROM chains \
        WHERE chain_id=?"
        );
    for (size_t sub_chain = parent_width; 
            sub_chain < parent_width + child_width; ++sub_chain)
    {
        tween_chains.reset();
        tween_chains.bind(sub_chain);
        tween_chains.bind(parent_span.left);
        tween_chains.exec();
    }
}

void postgresql_organizer::position_child_branch(
    size_t old_space, size_t new_space, size_t new_depth, size_t new_span_left)
{
    static cppdb::statement statement = sql_.prepare(
        "UPDATE blocks \
        SET \
            space=?, \
            depth = depth + ?, \
            span_left = span_left + ?, \
            span_right = span_right + ? \
        WHERE space=?"
        );
    statement.reset();
    statement.bind(new_space);
    statement.bind(new_depth);
    statement.bind(new_span_left);
    statement.bind(new_span_left);
    statement.bind(old_space);
    statement.exec();
}

postgresql_reader::postgresql_reader(cppdb::session sql)
  : sql_(sql)
{
}

script postgresql_reader::select_script(size_t script_id)
{
    static cppdb::statement statement = sql_.prepare(
        "SELECT \
            opcode, \
            data \
        FROM operations \
        WHERE script_id=? \
        ORDER BY operation_id ASC"
        );
    statement.reset();
    statement.bind(script_id);
    cppdb::result result = statement.query();
    script scr;
    while (result.next())
    {
        operation op;
        op.code = string_to_opcode(result.get<std::string>("opcode"));
        if (!result.is_null("data"))
            op.data = deserialize_bytes(result.get<std::string>("data"));
        scr.push_operation(op);
    }
    return scr;
}

message::transaction_input_list postgresql_reader::select_inputs(
        size_t transaction_id)
{
    static cppdb::statement statement = sql_.prepare(
        "SELECT * \
        FROM inputs \
        WHERE transaction_id=? \
        ORDER BY index_in_parent ASC"
        );
    statement.reset();
    statement.bind(transaction_id);
    cppdb::result result = statement.query();
    message::transaction_input_list inputs;
    while (result.next())
    {
        message::transaction_input input;
        input.hash = 
            deserialize_hash(result.get<std::string>("previous_output_hash"));
        input.index = result.get<uint32_t>("previous_output_index");
        size_t script_id = result.get<size_t>("script_id");
        input.input_script = select_script(script_id);
        input.sequence = result.get<uint32_t>("sequence");
        inputs.push_back(input);
    }
    return inputs;
}
message::transaction_output_list postgresql_reader::select_outputs(
        size_t transaction_id)
{
    static cppdb::statement statement = sql_.prepare(
        "SELECT \
            *, \
            sql_to_internal(value) internal_value \
        FROM outputs \
        WHERE transaction_id=? \
        ORDER BY index_in_parent ASC"
        );
    statement.reset();
    statement.bind(transaction_id);
    cppdb::result result = statement.query();
    message::transaction_output_list outputs;
    while (result.next())
    {
        message::transaction_output output;
        output.value = result.get<uint64_t>("internal_value");
        size_t script_id = result.get<size_t>("script_id");
        output.output_script = select_script(script_id);
        outputs.push_back(output);
    }
    return outputs;
}

message::transaction_list postgresql_reader::read_transactions(
        cppdb::result result)
{
    message::transaction_list transactions;
    while (result.next())
    {
        message::transaction transaction;
        transaction.version = result.get<uint32_t>("version");
        transaction.locktime = result.get<uint32_t>("locktime");
        size_t transaction_id = result.get<size_t>("transaction_id");
        transaction.inputs = select_inputs(transaction_id);
        transaction.outputs = select_outputs(transaction_id);
        transactions.push_back(transaction);
    }
    return transactions;
}

message::block postgresql_reader::read_block(cppdb::result block_result)
{
    message::block block;
    size_t block_id = block_result.get<size_t>("block_id");
    block.version = block_result.get<uint32_t>("version");
    block.timestamp = block_result.get<uint32_t>("timest");
    uint32_t bits_head = block_result.get<uint32_t>("bits_head"),
            bits_body = block_result.get<uint32_t>("bits_body");
    block.bits = bits_body + (bits_head << (3*8));
    block.nonce = block_result.get<uint32_t>("nonce");

    block.prev_block = 
            deserialize_hash(block_result.get<std::string>("prev_block_hash"));
    block.merkle_root = 
            deserialize_hash(block_result.get<std::string>("merkle"));

    static cppdb::statement transactions_statement = sql_.prepare(
        "SELECT transactions.* \
        FROM transactions_parents \
        JOIN transactions \
        ON transactions.transaction_id=transactions_parents.transaction_id \
        WHERE block_id=? \
        ORDER BY index_in_block ASC"
        );
    transactions_statement.reset();
    transactions_statement.bind(block_id);
    cppdb::result transactions_result = transactions_statement.query();
    block.transactions = read_transactions(transactions_result);
    return block;
}

postgresql_block_info postgresql_reader::read_block_info(
    cppdb::result result)
{
    BITCOIN_ASSERT(!result.is_null("prev_block_id"));
    return {
        result.get<size_t>("block_id"),
        result.get<size_t>("depth"),
        result.get<size_t>("span_left"),
        result.get<size_t>("span_right"),
        result.get<size_t>("prev_block_id")
    };
}

postgresql_validate_block::postgresql_validate_block(cppdb::session sql, 
    dialect_ptr dialect, const postgresql_block_info& block_info,
    const message::block& current_block)
  : validate_block(dialect, block_info.depth, current_block), 
    postgresql_reader(sql),
    sql_(sql), block_info_(block_info), current_block_(current_block)
{
}

uint32_t postgresql_validate_block::previous_block_bits()
{
    static cppdb::statement previous = sql_.prepare(
        "SELECT bits_head, bits_body \
        FROM blocks \
        WHERE \
            space = 0 \
            AND depth = ? - 1 \
            AND span_left <= ? \
            AND span_right >= ?"
        );
    previous.reset();
    previous.bind(block_info_.depth);
    previous.bind(block_info_.span_left);
    previous.bind(block_info_.span_right);
    cppdb::result result = previous.row();
    uint32_t bits_head = result.get<uint32_t>("bits_head"),
            bits_body = result.get<uint32_t>("bits_body");
    // TODO: Should use shared function with read_block(...)
    return bits_body + (bits_head << (3*8));
}

uint64_t postgresql_validate_block::actual_timespan(const uint64_t interval)
{
    BITCOIN_ASSERT(block_info_.depth >= interval);
    size_t begin_block_depth = block_info_.depth - interval,
        end_block_depth = block_info_.depth - 1;
    static cppdb::statement find_start = sql_.prepare(
        "SELECT EXTRACT(EPOCH FROM \
            end_block.when_created - start_block.when_created) \
        FROM \
            blocks as start_block, \
            blocks as end_block \
        WHERE \
            start_block.space = 0 \
            AND start_block.depth = ? \
            AND start_block.span_left <= ? \
            AND start_block.span_right >= ? \
            \
            AND end_block.space = 0 \
            AND end_block.depth = ? \
            AND end_block.span_left <= ? \
            AND end_block.span_right >= ?"
        );
    find_start.reset();
    find_start.bind(begin_block_depth);
    find_start.bind(block_info_.span_left);
    find_start.bind(block_info_.span_right);
    find_start.bind(end_block_depth);
    find_start.bind(block_info_.span_left);
    find_start.bind(block_info_.span_right);
    cppdb::result result = find_start.row();
    return result.get<uint32_t>(0);
}

uint64_t postgresql_validate_block::median_time_past()
{
    BITCOIN_ASSERT(block_info_.depth > 0);
    size_t median_offset = 5;
    if (block_info_.depth < 11)
        median_offset = block_info_.depth / 2;

    static cppdb::statement find_median = sql_.prepare(
        "SELECT EXTRACT(EPOCH FROM when_created) \
        FROM blocks \
        WHERE \
            space = 0 \
            AND depth < ? \
            AND depth >= ? - 11 \
            AND span_left <= ? \
            AND span_right >= ? \
        ORDER BY when_created \
        LIMIT 1 \
        OFFSET ?"
        );
    find_median.reset();
    find_median.bind(block_info_.depth);
    find_median.bind(block_info_.depth);
    find_median.bind(block_info_.span_left);
    find_median.bind(block_info_.span_right);
    find_median.bind(median_offset);
    cppdb::result result = find_median.row();
    return result.get<uint32_t>(0);
}

bool postgresql_validate_block::validate_transaction(
    const message::transaction& tx, size_t index_in_block, 
    uint64_t& value_in)
{
    static cppdb::statement find_transaction_id = sql_.prepare(
        "SELECT transaction_id \
        FROM transactions_parents \
        WHERE \
            block_id=? \
            AND index_in_block=?"
        );
    find_transaction_id.reset();
    find_transaction_id.bind(block_info_.block_id);
    find_transaction_id.bind(index_in_block);
    cppdb::result transaction_id_result = find_transaction_id.row();
    BITCOIN_ASSERT(!transaction_id_result.empty());
    size_t transaction_id = transaction_id_result.get<size_t>(0);

    BITCOIN_ASSERT(!is_coinbase(tx));
    for (size_t input_index = 0; input_index < tx.inputs.size(); ++input_index)
        if (!connect_input(transaction_id, tx, input_index, value_in))
            return false;
    // select * from inputs as i1, inputs as i2 where i1.input_id=192 and
    // i1.previous_output_hash=i2.previous_output_hash and
    // i1.previous_output_index=i2.previous_output_index and
    // i2.input_id!=i1.input_id;
    return true;
}

bool postgresql_validate_block::connect_input(
    size_t transaction_id, const message::transaction& current_tx, 
    size_t input_index, uint64_t& value_in)
{
    BITCOIN_ASSERT(input_index < current_tx.inputs.size());
    const message::transaction_input& input = current_tx.inputs[input_index];
    std::string hash_repr = hexlify(input.hash);
    static cppdb::statement find_previous_tx = sql_.prepare(
        "SELECT transaction_id \
        FROM transactions \
        WHERE transaction_hash=?"
        );
    find_previous_tx.reset();
    find_previous_tx.bind(hash_repr);
    cppdb::result previous_tx = find_previous_tx.row();
    if (previous_tx.empty())
        return false;
    size_t previous_tx_id = previous_tx.get<size_t>(0);
    static cppdb::statement find_previous_output = sql_.prepare(
        "SELECT \
            output_id, \
            script_id, \
            sql_to_internal(value) \
        FROM outputs \
        WHERE \
            transaction_id=? \
            AND index_in_parent=?"
        );
    find_previous_output.reset();
    find_previous_output.bind(previous_tx_id);
    find_previous_output.bind(input.index);
    cppdb::result previous_output = find_previous_output.row();
    if (previous_output.empty())
        return false;
    size_t output_id = previous_output.get<size_t>(0);
    size_t output_script_id = previous_output.get<size_t>(1);
    uint64_t output_value = previous_output.get<uint64_t>(2);
    if (output_value > max_money())
        return false;
    if (is_coinbase_transaction(previous_tx_id))
    {
        // Check whether generated coin has sufficiently matured
        size_t depth_difference =
            previous_block_depth(previous_tx_id) - block_info_.depth;
        if (depth_difference < coinbase_maturity)
            return false;
    }
    script output_script = select_script(output_script_id);
    if (!output_script.run(input.input_script, current_tx, input_index))
        return false;
    if (search_double_spends(transaction_id, input, input_index))
        return false;
    value_in += output_value;
    if (value_in > max_money())
        return false;
    return true;
}

bool postgresql_validate_block::is_coinbase_transaction(size_t tx_id)
{
    static cppdb::statement fetch_params = sql_.prepare(
        "SELECT \
            previous_output_hash, \
            previous_output_index \
        FROM inputs \
        WHERE transaction_id=?"
        );
    fetch_params.reset();
    fetch_params.bind(tx_id);
    cppdb::result params = fetch_params.query();
    message::transaction partial;
    while (params.next())
    {
        message::transaction_input input;
        input.hash =
            deserialize_hash(params.get<std::string>(0));
        input.index = params.get<size_t>(1);
        partial.inputs.push_back(input);
    }
    return is_coinbase(partial);
}

size_t postgresql_validate_block::previous_block_depth(size_t previous_tx_id)
{
    static cppdb::statement hookup_block = sql_.prepare(
        "SELECT depth \
        FROM \
            transactions_parents, \
            blocks \
        WHERE \
            transaction_id=? \
            AND transactions_parents.block_id=blocks.block_id \
            AND space=0 \
            AND span_left <= ? \
            AND span_right >= ?"
        );
    hookup_block.reset();
    hookup_block.bind(previous_tx_id);
    hookup_block.bind(block_info_.span_left);
    hookup_block.bind(block_info_.span_right);
    cppdb::result result = hookup_block.row();
    BITCOIN_ASSERT(!result.empty());
    return result.get<size_t>(0);
}

bool postgresql_validate_block::search_double_spends(size_t transaction_id, 
    const message::transaction_input& input, size_t input_index)
{
    // What is this input id?
    //   WHERE transaction_id=... AND index_in_parent=...

    // Has this output been already spent by another input?
    std::string hash_repr = hexlify(input.hash);
    static cppdb::statement search_spends = sql_.prepare(
        "SELECT input_id \
        FROM inputs \
        WHERE \
            previous_output_hash=? \
            AND previous_output_index=? \
            AND ( \
                transaction_id != ? \
                OR index_in_parent != ? \
            )"
        );
    search_spends.reset();
    search_spends.bind(hash_repr);
    search_spends.bind(input.index);
    search_spends.bind(transaction_id);
    search_spends.bind(input_index);
    cppdb::result other_spends = search_spends.query();
    if (other_spends.empty())
        return false;
    log_fatal() << "Search other spends in other branches implemented!";
    // Is that input in the same branch as us?
    // - Loop through blocks containing that input
    // - Check if in same branch
    return true;
}

postgresql_blockchain::postgresql_blockchain(
        cppdb::session sql, service_ptr service)
  : postgresql_organizer(sql), postgresql_reader(sql),
    barrier_clearance_level_(400), barrier_timeout_(milliseconds(500)), 
    sql_(sql)
{
    timeout_.reset(new deadline_timer(*service));
    reset_state();
    start();
}

void postgresql_blockchain::set_clearance(size_t clearance)
{
    barrier_clearance_level_ = clearance;
}
void postgresql_blockchain::set_timeout(time_duration timeout)
{
    barrier_timeout_ = timeout;
}

void postgresql_blockchain::raise_barrier()
{
    barrier_level_++;
    if (barrier_level_ > barrier_clearance_level_)
    {
        reset_state();
        start();
    }
    else if (!timer_started_)
    {
        timer_started_ = true;
        timeout_->expires_from_now(barrier_timeout_);
        timeout_->async_wait(std::bind( 
            &postgresql_blockchain::start_exec, shared_from_this(), _1));
    }
}

void postgresql_blockchain::reset_state()
{
    timeout_->cancel();
    barrier_level_ = 0;
    timer_started_ = false;
}

void postgresql_blockchain::start_exec(const boost::system::error_code& ec)
{
    reset_state();
    if (ec == boost::asio::error::operation_aborted)
    {
        return;
    }
    else if (ec)
    {
        log_fatal() << "Blockchain processing: " << ec.message();
        return;
    }
    start();
}

void postgresql_blockchain::start()
{
    organize();
    validate();
}

void postgresql_blockchain::validate()
{
    dialect_.reset(new original_dialect);
    static cppdb::statement statement = sql_.prepare(
        "SELECT \
            *, \
            EXTRACT(EPOCH FROM when_created) timest \
        FROM blocks \
        WHERE \
            status='orphan' \
            AND space=0  \
        ORDER BY depth ASC"
        );
    statement.reset();
    cppdb::result result = statement.query();
    // foreach invalid block where status = orphan
    // do verification and set status = valid
    while (result.next())
    {
        const postgresql_block_info block_info = read_block_info(result);
        const message::block current_block = read_block(result);

        postgresql_validate_block block_validation(
            sql_, dialect_, block_info, current_block);

        if (block_validation.validates())
            finalize_status(block_info, current_block);
        else
        {
            log_error() << "Block " << block_info.block_id
                << " failed validation!";
            // TODO: Should delete this branch
            exit(-1);
            break;
        }
    }
    // TODO: Request new blocks + broadcast new blocks
}

void postgresql_blockchain::finalize_status(
    const postgresql_block_info& block_info, 
    const message::block& current_block)
{
    // TODO: together with serialise functions, should be in psql_helper.hpp
    uint32_t bits_head = (current_block.bits >> (8*3)),
        bits_body = (current_block.bits & 0x00ffffff);
    // TODO: Should be prepared statements. Too lazy ATM
    // TODO: This should be atomic
    sql_ <<
        "UPDATE chains \
        SET \
            work = work + difficulty(?, ?), \
            depth = ? \
        WHERE \
            chain_id >= ? \
            AND chain_id <= ?" 
        << bits_head
        << bits_body
        << block_info.depth
        << block_info.span_left
        << block_info.span_right
        << cppdb::exec;

    sql_ <<
        "UPDATE blocks \
        SET status='valid' \
        WHERE block_id=?"
        << block_info.block_id
        << cppdb::exec;
}

} // libbitcoin

