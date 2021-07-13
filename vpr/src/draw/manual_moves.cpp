#include "manual_moves.h"
#include "move_utils.h"
#include "globals.h"
#include "draw.h"
#include "move_generator.h"
#include  "buttons.h"
#include "move_utils.h"

#ifndef NO_GRAPHICS

//Global Variables
ManualMovesGlobals manual_moves_global;

void draw_manual_moves_window(std::string block_id) {

	if (!manual_moves_global.mm_window_is_open) {
		manual_moves_global.mm_window_is_open = true;

		//Window settings-
		manual_moves_global.manual_move_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		gtk_window_set_position((GtkWindow*) manual_moves_global.manual_move_window, GTK_WIN_POS_CENTER);
		gtk_window_set_title((GtkWindow*) manual_moves_global.manual_move_window, "Manual Moves Generator");
		gtk_widget_set_name(manual_moves_global.manual_move_window, "manual_move_window");

		GtkWidget *grid = gtk_grid_new();
		GtkWidget *block_entry = gtk_entry_new();

		if (manual_moves_global.user_highlighted_block) {
			gtk_entry_set_text((GtkEntry*) block_entry, block_id.c_str());
			manual_moves_global.user_highlighted_block = false;
		}

		GtkWidget* x_position_entry = gtk_entry_new();
		GtkWidget* y_position_entry = gtk_entry_new();
		GtkWidget* subtile_position_entry = gtk_entry_new();
		GtkWidget* block_label = gtk_label_new("Block ID/Block Name:");
		GtkWidget* to_label = gtk_label_new("To Location:");
		GtkWidget* x = gtk_label_new("x:");
		GtkWidget* y = gtk_label_new("y:");
		GtkWidget* subtile = gtk_label_new("Subtile:");

		GtkWidget* calculate_cost_button = gtk_button_new_with_label("Calculate Costs");

		//Add all to grid
		gtk_grid_attach((GtkGrid*)grid, block_label, 0, 0, 1, 1);
		gtk_grid_attach((GtkGrid*)grid, block_entry, 0, 1, 1, 1);
		gtk_grid_attach((GtkGrid*)grid, to_label, 2, 0, 1, 1);
		gtk_grid_attach((GtkGrid*)grid, x, 1, 1, 1, 1);
		gtk_grid_attach((GtkGrid*)grid, x_position_entry, 2, 1, 1, 1);
		gtk_grid_attach((GtkGrid*)grid, y, 1, 2, 1, 1);
		gtk_grid_attach((GtkGrid*)grid, y_position_entry, 2, 2, 1, 1);
		gtk_grid_attach((GtkGrid*)grid, subtile, 1, 3, 1, 1);
		gtk_grid_attach((GtkGrid*)grid, subtile_position_entry, 2, 3, 1, 1);
		gtk_grid_attach((GtkGrid*)grid, calculate_cost_button, 0, 4, 3, 1); //spans three columns

		//Set margins
		gtk_widget_set_margin_bottom(grid, 20);
		gtk_widget_set_margin_top(grid, 20);
		gtk_widget_set_margin_start(grid, 20);
		gtk_widget_set_margin_end(grid, 20);
		gtk_widget_set_margin_bottom(block_label, 5);
		gtk_widget_set_margin_bottom(to_label, 5);
		gtk_widget_set_margin_top(calculate_cost_button, 15);
		gtk_widget_set_margin_start(x, 13);
		gtk_widget_set_margin_start(y, 13);
		gtk_widget_set_halign(calculate_cost_button, GTK_ALIGN_CENTER);

		//connect signals
		g_signal_connect(calculate_cost_button, "clicked", G_CALLBACK(calculate_cost_callback), grid);
		g_signal_connect(G_OBJECT(manual_moves_global.manual_move_window), "destroy", G_CALLBACK(close_manual_moves_window), NULL);


		gtk_container_add(GTK_CONTAINER(manual_moves_global.manual_move_window), grid);
		gtk_widget_show_all(manual_moves_global.manual_move_window);
	}
}

void calculate_cost_callback(GtkWidget* /*widget*/, GtkWidget* grid) {

	int block_id;
	int x_location;
	int y_location;
	int subtile_location;
	bool valid_input = true;

	//Loading the context/data structures needed.
	auto &cluster_ctx = g_vpr_ctx.clustering();
	auto &place_ctx = g_vpr_ctx.placement();

	//Getting entry values
	GtkWidget *block_entry = gtk_grid_get_child_at((GtkGrid*) grid, 0, 1);
	std::string block_id_string = gtk_entry_get_text((GtkEntry*) block_entry);

	if (string_is_a_number(block_id_string)) { //for block ID
		block_id = std::atoi(block_id_string.c_str());
	} else { //for block name
		block_id = size_t(cluster_ctx.clb_nlist.find_block(gtk_entry_get_text((GtkEntry*) block_entry)));
	}
	//if the block is not found 
	if ((!cluster_ctx.clb_nlist.valid_block_id(ClusterBlockId(block_id)))) {
		invalid_breakpoint_entry_window("Invalid block ID/Name");
		valid_input = false;
	}

	GtkWidget *x_position_entry = gtk_grid_get_child_at((GtkGrid*) grid, 2, 1);
	GtkWidget *y_position_entry = gtk_grid_get_child_at((GtkGrid*) grid, 2, 2);
	GtkWidget *subtile_position_entry = gtk_grid_get_child_at((GtkGrid*) grid, 2, 3);

	x_location = std::atoi(gtk_entry_get_text((GtkEntry*) x_position_entry));
	y_location = std::atoi(gtk_entry_get_text((GtkEntry*) y_position_entry));
	subtile_location = std::atoi(gtk_entry_get_text((GtkEntry*) subtile_position_entry));

	//Function in move_utils.cpp that returns true if the location swap is valid
	t_pl_loc to = t_pl_loc(x_location, y_location, subtile_location);
	if (!is_legal_swap_to_location(ClusterBlockId(block_id), to)) {
		valid_input = false;
	}
	//If the block requested is already in that location.
	ClusterBlockId current_block = ClusterBlockId(block_id);
	t_pl_loc current_block_loc = place_ctx.block_locs[current_block].loc;
	if (x_location == current_block_loc.x && y_location == current_block_loc.y && subtile_location == current_block_loc.sub_tile) {
		invalid_breakpoint_entry_window("The block is currently in this location");
		valid_input = false;
	}
	//Checks if all fields from the user input window are complete.
	if (std::string(gtk_entry_get_text((GtkEntry*) block_entry)).empty() || std::string(gtk_entry_get_text((GtkEntry*) x_position_entry)).empty() || std::string(gtk_entry_get_text((GtkEntry*) y_position_entry)).empty() || std::string(gtk_entry_get_text((GtkEntry*) subtile_position_entry)).empty()) {
		invalid_breakpoint_entry_window("Not all fields are complete");
		valid_input = false;
	}

	if (valid_input) {
		manual_moves_global.manual_move_info.valid_input = true;
		manual_moves_global.manual_move_info.blockID = block_id;
		manual_moves_global.manual_move_info.x_pos = x_location;
		manual_moves_global.manual_move_info.y_pos = y_location;
		manual_moves_global.manual_move_info.subtile = subtile_location;
		manual_moves_global.manual_move_info.to_location = to;

		//Highlighting the block
		deselect_all();
		ClusterBlockId clb_index = ClusterBlockId(manual_moves_global.manual_move_info.blockID);
		draw_highlight_blocks_color(cluster_ctx.clb_nlist.block_type(clb_index), clb_index);
		application.refresh_drawing();

		//Continues to move costs window.
		GtkWidget *proceed = find_button("ProceedButton");
		ezgl::press_proceed(proceed, &application);

	} else {
		manual_moves_global.manual_move_info.valid_input = false;
	}
}

bool string_is_a_number(std::string block_id) {
	for (size_t i = 0; i < block_id.size(); i++) {
		//Returns 0 if the string does not have characters from 0-9
		if (isdigit(block_id[i]) == 0) {
			return false;
		}
	}
	return true;
}

void get_manual_move_flag() {
	GObject *manual_moves = application.get_object("manualMove");
	//return gtk_toggle_button_get_active((GtkToggleButton*) manual_moves);
	manual_moves_global.manual_move_flag = gtk_toggle_button_get_active((GtkToggleButton*)manual_moves);
	//return manual_moves_global.manual_move_flag;
}

ManualMovesGlobals* get_manual_moves_global() {
	return &manual_moves_global;
}


//Manual Move Generator function
e_create_move ManualMoveGenerator::propose_move(t_pl_blocks_to_be_moved &blocks_affected, float /*rlim*/) {

	int block_id = manual_moves_global.manual_move_info.blockID;
	t_pl_loc to = manual_moves_global.manual_move_info.to_location;
	ClusterBlockId b_from = ClusterBlockId(block_id);

	//Checking if the block was found
	if (!b_from) {
		return e_create_move::ABORT; //No movable block was found
	}

	auto &place_ctx = g_vpr_ctx.placement();
	auto &cluster_ctx = g_vpr_ctx.clustering();
	auto &device_ctx = g_vpr_ctx.device();

	//Gets the current location of the block to move.
	t_pl_loc from = place_ctx.block_locs[b_from].loc;
	auto cluster_from_type = cluster_ctx.clb_nlist.block_type(b_from);
	auto grid_from_type = device_ctx.grid[from.x][from.y].type;
	VTR_ASSERT(is_tile_compatible(grid_from_type, cluster_from_type));

	//Retrieving the compressed block grid for this block type
	const auto &compressed_block_grid =
			place_ctx.compressed_block_grids[cluster_from_type->index];
	//Checking if the block has a compatible subtile.
	auto to_type = device_ctx.grid[to.x][to.y].type;
	auto& compatible_subtiles = compressed_block_grid.compatible_sub_tiles_for_tile.at(to_type->index);

	//No compatible subtile is found.
	if (std::find(compatible_subtiles.begin(), compatible_subtiles.end(), to.sub_tile) == compatible_subtiles.end()) {
		return e_create_move::ABORT;
	}

	e_create_move create_move = ::create_move(blocks_affected, b_from, to);
	return create_move;
}

void cost_summary_dialog() {

	GtkWidget *dialog;
	GtkWidget *content_area;

	//Creating the dialog window
	dialog = gtk_dialog_new_with_buttons("Move Costs",
										 (GtkWindow*)manual_moves_global.manual_move_window,
										 GTK_DIALOG_DESTROY_WITH_PARENT,
										 ("Accept"),
										 GTK_RESPONSE_ACCEPT,
										 ("Reject"),
										 GTK_RESPONSE_REJECT,
										 NULL);

	gtk_widget_set_halign(gtk_dialog_get_action_area((GtkDialog*) dialog), GTK_ALIGN_CENTER);

	//Create elements for the dialog and printing costs to the user.
	GtkWidget *title_label = gtk_label_new(NULL);
	gtk_label_set_markup((GtkLabel*) title_label, "<b>Move Costs and Outcomes</b>");
	std::string delta_cost = "Delta Cost: " + std::to_string(manual_moves_global.manual_move_info.delta_cost)	+ "   ";
	GtkWidget *delta_cost_label = gtk_label_new(delta_cost.c_str());
	std::string delta_timing = "   Delta Timing: " + std::to_string(manual_moves_global.manual_move_info.delta_timing)	+ "   ";
	GtkWidget *delta_timing_label = gtk_label_new(delta_timing.c_str());
	std::string delta_bounding_box = "  Delta Bounding Box Cost: " + std::to_string(manual_moves_global.manual_move_info.delta_bounding_box) + "   ";
	GtkWidget *delta_bounding_box_label = gtk_label_new(delta_bounding_box.c_str());
	std::string outcome = e_move_result_to_string(manual_moves_global.manual_move_info.placer_move_outcome);
	std::string move_outcome = "  Annealing Decision: " + outcome + "   ";
	GtkWidget *move_outcome_label = gtk_label_new(move_outcome.c_str());
	GtkWidget *space_label1 = gtk_label_new("    ");
	GtkWidget *space_label2 = gtk_label_new("    ");

	//Attach elements to the content area of the dialog.
	content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	gtk_container_add(GTK_CONTAINER(content_area), title_label);
	gtk_container_add(GTK_CONTAINER(content_area), space_label1);
	gtk_container_add(GTK_CONTAINER(content_area), delta_cost_label);
	gtk_container_add(GTK_CONTAINER(content_area), delta_timing_label);
	gtk_container_add(GTK_CONTAINER(content_area), delta_bounding_box_label);
	gtk_container_add(GTK_CONTAINER(content_area), move_outcome_label);
	gtk_container_add(GTK_CONTAINER(content_area), space_label2);

	//Show the dialog with all the labels.
	gtk_widget_show_all(dialog);

	//Update message if user accepts the move.
	std::string msg = "Manual move accepted. Block #" + std::to_string(manual_moves_global.manual_move_info.blockID);
	msg += " to location (" + std::to_string(manual_moves_global.manual_move_info.x_pos) + ", " + std::to_string(manual_moves_global.manual_move_info.y_pos) + ")";

	//Waiting for the user to respond to return to try_swa function.
	int result = gtk_dialog_run(GTK_DIALOG(dialog));
	switch(result) {

	//If the user accepts the manual move
	case GTK_RESPONSE_ACCEPT:
		manual_moves_global.manual_move_info.user_move_outcome = ACCEPTED;
		application.update_message(msg.c_str());
		break;
	//If the user rejects the manual move
	case GTK_RESPONSE_REJECT:
		manual_moves_global.manual_move_info.user_move_outcome = REJECTED;
		application.update_message("Manual move was rejected");
		break;
	default:
		manual_moves_global.manual_move_info.user_move_outcome = ABORTED;
		break;

	}

	//Destroys the move outcome dialog.
	gtk_widget_destroy(dialog);

}

void highlight_new_block_location(bool manual_move_flag) {
	if(manual_move_flag) {
		auto& cluster_ctx = g_vpr_ctx.clustering();
		//Unselects all blocks first
		deselect_all();
		//Highlighting the block
		ClusterBlockId clb_index = ClusterBlockId(manual_moves_global.manual_move_info.blockID);
		draw_highlight_blocks_color(cluster_ctx.clb_nlist.block_type(clb_index), clb_index);
		application.refresh_drawing();
	}
}

//Updates ManualMovesInfo cost members
void update_manual_move_costs(double d_cost, double d_timing, double d_bounding_box, e_move_result& move_outcome) {
	manual_moves_global.manual_move_info.delta_cost = d_cost;
	manual_moves_global.manual_move_info.delta_timing = d_timing;
	manual_moves_global.manual_move_info.delta_bounding_box = d_bounding_box;
	manual_moves_global.manual_move_info.placer_move_outcome = move_outcome;
}

//Manual move window turns false, the window is destroyed.
void close_manual_moves_window() {
	manual_moves_global.mm_window_is_open = false;
}

//Deactivates the toggle button once the windows close.
void deactivating_toggle_button() {
	GObject *manual_move_toggle = application.get_object("manualMove");
	gtk_toggle_button_set_active((GtkToggleButton*) manual_move_toggle, FALSE);
}

#endif
