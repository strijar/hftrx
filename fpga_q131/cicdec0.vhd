-- megafunction wizard: %FIR Compiler v13.1%
-- GENERATION: XML

-- ============================================================
-- Megafunction Name(s):
-- 			cicdec0_ast
-- ============================================================
-- Generated by FIR Compiler 13.1 [Altera, IP Toolbench 1.3.0 Build 182]
-- ************************************************************
-- THIS IS A WIZARD-GENERATED FILE. DO NOT EDIT THIS FILE!
-- ************************************************************
-- Copyright (C) 1991-2021 Altera Corporation
-- Any megafunction design, and related net list (encrypted or decrypted),
-- support information, device programming or simulation file, and any other
-- associated documentation or information provided by Altera or a partner
-- under Altera's Megafunction Partnership Program may be used only to
-- program PLD devices (but not masked PLD devices) from Altera.  Any other
-- use of such megafunction design, net list, support information, device
-- programming or simulation file, or any other related documentation or
-- information is prohibited for any other purpose, including, but not
-- limited to modification, reverse engineering, de-compiling, or use with
-- any other silicon devices, unless such use is explicitly licensed under
-- a separate agreement with Altera or a megafunction partner.  Title to
-- the intellectual property, including patents, copyrights, trademarks,
-- trade secrets, or maskworks, embodied in any such megafunction design,
-- net list, support information, device programming or simulation file, or
-- any other related documentation or information provided by Altera or a
-- megafunction partner, remains with Altera, the megafunction partner, or
-- their respective licensors.  No other licenses, including any licenses
-- needed under any third party's intellectual property, are provided herein.

library IEEE;
use IEEE.std_logic_1164.all;

ENTITY cicdec0 IS
	PORT (
		clk	: IN STD_LOGIC;
		reset_n	: IN STD_LOGIC;
		ast_sink_data	: IN STD_LOGIC_VECTOR (27 DOWNTO 0);
		coef_set	: IN STD_LOGIC;
		ast_sink_valid	: IN STD_LOGIC;
		ast_source_ready	: IN STD_LOGIC;
		ast_sink_sop	: IN STD_LOGIC;
		ast_sink_eop	: IN STD_LOGIC;
		ast_sink_error	: IN STD_LOGIC_VECTOR (1 DOWNTO 0);
		coef_set_in	: IN STD_LOGIC;
		coef_we	: IN STD_LOGIC;
		coef_in	: IN STD_LOGIC_VECTOR (23 DOWNTO 0);
		coef_in_clk	: IN STD_LOGIC;
		ast_source_data	: OUT STD_LOGIC_VECTOR (27 DOWNTO 0);
		ast_sink_ready	: OUT STD_LOGIC;
		ast_source_valid	: OUT STD_LOGIC;
		ast_source_sop	: OUT STD_LOGIC;
		ast_source_eop	: OUT STD_LOGIC;
		ast_source_channel	: OUT STD_LOGIC;
		ast_source_error	: OUT STD_LOGIC_VECTOR (1 DOWNTO 0)
	);
END cicdec0;

ARCHITECTURE SYN OF cicdec0 IS


	COMPONENT cicdec0_ast
	PORT (
		clk	: IN STD_LOGIC;
		reset_n	: IN STD_LOGIC;
		ast_sink_data	: IN STD_LOGIC_VECTOR (27 DOWNTO 0);
		coef_set	: IN STD_LOGIC;
		ast_sink_valid	: IN STD_LOGIC;
		ast_source_ready	: IN STD_LOGIC;
		ast_sink_sop	: IN STD_LOGIC;
		ast_sink_eop	: IN STD_LOGIC;
		ast_sink_error	: IN STD_LOGIC_VECTOR (1 DOWNTO 0);
		coef_set_in	: IN STD_LOGIC;
		coef_we	: IN STD_LOGIC;
		coef_in	: IN STD_LOGIC_VECTOR (23 DOWNTO 0);
		coef_in_clk	: IN STD_LOGIC;
		ast_source_data	: OUT STD_LOGIC_VECTOR (27 DOWNTO 0);
		ast_sink_ready	: OUT STD_LOGIC;
		ast_source_valid	: OUT STD_LOGIC;
		ast_source_sop	: OUT STD_LOGIC;
		ast_source_eop	: OUT STD_LOGIC;
		ast_source_channel	: OUT STD_LOGIC;
		ast_source_error	: OUT STD_LOGIC_VECTOR (1 DOWNTO 0)
	);

	END COMPONENT;

BEGIN

	cicdec0_ast_inst : cicdec0_ast
	PORT MAP (
		clk  =>  clk,
		reset_n  =>  reset_n,
		ast_sink_data  =>  ast_sink_data,
		ast_source_data  =>  ast_source_data,
		coef_set  =>  coef_set,
		ast_sink_valid  =>  ast_sink_valid,
		ast_sink_ready  =>  ast_sink_ready,
		ast_source_valid  =>  ast_source_valid,
		ast_source_ready  =>  ast_source_ready,
		ast_sink_sop  =>  ast_sink_sop,
		ast_sink_eop  =>  ast_sink_eop,
		ast_source_sop  =>  ast_source_sop,
		ast_source_eop  =>  ast_source_eop,
		ast_source_channel  =>  ast_source_channel,
		ast_sink_error  =>  ast_sink_error,
		ast_source_error  =>  ast_source_error,
		coef_set_in  =>  coef_set_in,
		coef_we  =>  coef_we,
		coef_in  =>  coef_in,
		coef_in_clk  =>  coef_in_clk
	);


END SYN;


-- =========================================================
-- FIR Compiler Wizard Data
-- ===============================
-- DO NOT EDIT FOLLOWING DATA
-- @Altera, IP Toolbench@
-- Warning: If you modify this section, FIR Compiler Wizard may not be able to reproduce your chosen configuration.
-- 
-- Retrieval info: <?xml version="1.0"?>
-- Retrieval info: <MEGACORE title="FIR Compiler"  version="13.1"  build="182"  iptb_version="1.3.0 Build 182"  format_version="120" >
-- Retrieval info:  <NETLIST_SECTION class="altera.ipbu.flowbase.netlist.model.FIRModelClass"  active_core="cicdec0_ast" >
-- Retrieval info:   <STATIC_SECTION>
-- Retrieval info:    <PRIVATES>
-- Retrieval info:     <NAMESPACE name = "parameterization">
-- Retrieval info:      <PRIVATE name = "use_mem" value="1"  type="BOOLEAN"  enable="1" />
-- Retrieval info:      <PRIVATE name = "mem_type" value="M512"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "filter_rate" value="Single Rate"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "filter_factor" value="4"  type="INTEGER"  enable="0" />
-- Retrieval info:      <PRIVATE name = "coefficient_scaling_type" value="Signed Binary Fractional"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "coefficient_scaling_factor" value="8388608.0"  type="STRING"  enable="0" />
-- Retrieval info:      <PRIVATE name = "coefficient_bit_width" value="24"  type="INTEGER"  enable="0" />
-- Retrieval info:      <PRIVATE name = "coefficient_binary_point_position" value="0"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "number_of_input_channels" value="2"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "input_number_system" value="Signed Binary"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "input_bit_width" value="28"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "input_binary_point_position" value="0"  type="INTEGER"  enable="0" />
-- Retrieval info:      <PRIVATE name = "output_bit_width_method" value="Bit Width Only"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "output_number_system" value="Custom Resolution"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "output_bit_width" value="28"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "output_bits_right_of_binary_point" value="28"  type="INTEGER"  enable="0" />
-- Retrieval info:      <PRIVATE name = "output_bits_removed_from_lsb" value="23"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "output_lsb_remove_type" value="Truncate"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "output_msb_remove_type" value="Truncate"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "flow_control" value="0"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "flow_control_input" value="Slave Sink"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "flow_control_output" value="Master Source"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "device_family" value="Cyclone IV E"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "structure" value="Variable/Fixed Coefficient : Multi-Cycle"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "pipeline_level" value="3"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "clocks_to_compute" value="512"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "number_of_serial_units" value="2"  type="INTEGER"  enable="0" />
-- Retrieval info:      <PRIVATE name = "data_storage" value="M9K"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "coefficient_storage" value="M9K"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "multiplier_storage" value="DSP Blocks"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "force_non_symmetric_structure" value="0"  type="BOOLEAN"  enable="1" />
-- Retrieval info:      <PRIVATE name = "coefficients_reload" value="1"  type="BOOLEAN"  enable="1" />
-- Retrieval info:      <PRIVATE name = "coefficients_reload_sgl_clock" value="0"  type="BOOLEAN"  enable="1" />
-- Retrieval info:      <PRIVATE name = "max_clocks_to_compute" value="512"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "set_1" value="Low Pass Set, Floating, Low Pass, Blackman, 1023, 48000.0, 1550.0, 3750000.0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -1.0473E-6, -1.17223E-6, -1.2541E-6, -1.28055E-6, -1.24088E-6, -1.12683E-6, 0.0, 0.0, 0.0, 0.0, 0.0, 1.09279E-6, 1.618E-6, 2.13243E-6, 2.60813E-6, 3.01612E-6, 3.32777E-6, 3.51644E-6, 3.55902E-6, 3.4375E-6, 3.14043E-6, 2.66409E-6, 2.01338E-6, 1.20237E-6, 0.0, 0.0, -1.91541E-6, -3.04996E-6, -4.14996E-6, -5.16062E-6, -6.0268E-6, -6.69564E-6, -7.11936E-6, -7.2579E-6, -7.0815E-6, -6.57284E-6, -5.72888E-6, -4.56196E-6, -3.10043E-6, -1.3884E-6, 0.0, 2.53827E-6, 4.59817E-6, 6.60467E-6, 8.4637E-6, 1.00815E-5, 1.13688E-5, 1.22456E-5, 1.26449E-5, 1.25172E-5, 1.18334E-5, 1.05877E-5, 8.79918E-6, 6.51244E-6, 3.79731E-6, 0.0, -2.52458E-6, -5.88682E-6, -9.19725E-6, -1.23073E-5, -1.50687E-5, -1.73398E-5, -1.89929E-5, -1.99201E-5, -2.00395E-5, -1.93004E-5, -1.76871E-5, -1.52215E-5, -1.19642E-5, -8.0137E-6, -3.50439E-6, 1.39782E-6, 6.50109E-6, 1.15948E-5, 1.64577E-5, 2.08674E-5, 2.46099E-5, 2.74896E-5, 2.93384E-5, 3.00248E-5, 2.94615E-5, 2.7611E-5, 2.44899E-5, 2.0171E-5, 1.4782E-5, 8.5036E-6, 1.56325E-6, -5.77215E-6, -1.32062E-5, -2.04243E-5, -2.7107E-5, -3.29429E-5, -3.76432E-5, -4.0955E-5, -4.26737E-5, -4.26543E-5, -4.08201E-5, -3.71691E-5, -3.17772E-5, -2.47986E-5, -1.64623E-5, -7.06546E-6, 3.03662E-6, 1.34431E-5, 2.37233E-5, 3.34335E-5, 4.21361E-5, 4.94182E-5, 5.49107E-5, 5.83058E-5, 5.93728E-5, 5.79712E-5, 5.406E-5, 4.7704E-5, 3.90751E-5, 2.84488E-5, 1.61969E-5, 2.77535E-6, -1.12922E-5, -2.5434E-5, -3.90529E-5, -5.15509E-5, -6.23545E-5, -7.09404E-5, -7.68599E-5, -7.97603E-5, -7.94043E-5, -7.5684E-5, -6.86304E-5, -5.84174E-5, -4.53596E-5, -2.99042E-5, -1.26166E-5, 5.8394E-6, 2.4726E-5, 4.32603E-5, 6.0646E-5, 7.6107E-5, 8.89208E-5, 9.84511E-5, 1.04179E-4, 1.05726E-4, 1.02881E-4, 9.56091E-5, 8.40613E-5, 6.85762E-5, 4.96701E-5, 2.80223E-5, 4.45135E-6, -2.01149E-5, -4.46746E-5, -6.81921E-5, -8.96409E-5, -1.08047E-4, -1.22532E-4, -1.32355E-4, -1.36945E-4, -1.35936E-4, -1.29183E-4, -1.16782E-4, -9.90687E-5, -7.66139E-5, -5.02078E-5, -2.08331E-5, 1.03708E-5, 4.21504E-5, 7.31874E-5, 1.02153E-4, 1.27762E-4, 1.48832E-4, 1.64332E-4, 1.73436E-4, 1.75557E-4, 1.70389E-4, 1.57919E-4, 1.38444E-4, 1.12564E-4, 8.11672E-5, 4.54024E-5, 6.63672E-6, -3.35942E-5, -7.36463E-5, -1.11833E-4, -1.46493E-4, -1.76068E-4, -1.9916E-4, -2.14607E-4, -2.21531E-4, -2.19387E-4, -2.07994E-4, -1.87554E-4, -1.58659E-4, -1.22268E-4, -7.96875E-5, -3.2521E-5, 1.73888E-5, 6.80297E-5, 1.17301E-4, 1.63097E-4, 2.034E-4, 2.36361E-4, 2.6039E-4, 2.74223E-4, 2.76991E-4, 2.68261E-4, 2.48073E-4, 2.16949E-4, 1.75881E-4, 1.26311E-4, 7.00735E-5, 9.33741E-6, -5.3481E-5, -1.1581E-4, -1.75029E-4, -2.28571E-4, -2.74041E-4, -3.09314E-4, -3.32633E-4, -3.42696E-4, -3.3872E-4, -3.20488E-4, -2.88376E-4, -2.4335E-4, -1.86944E-4, -1.21209E-4, -4.8644E-5, 2.79015E-5, 1.05334E-4, 1.8044E-4, 2.50019E-4, 3.11016E-4, 3.60655E-4, 3.96561E-4, 4.16871E-4, 4.20328E-4, 4.06345E-4, 3.75049E-4, 3.27296E-4, 2.64653E-4, 1.89348E-4, 1.04199E-4, 1.25074E-5, -8.20668E-5, -1.75649E-4, -2.64305E-4, -3.44209E-4, -4.118E-4, -4.63942E-4, -4.98065E-4, -5.12288E-4, -5.05513E-4, -4.7749E-4, -4.28852E-4, -3.61111E-4, -2.76612E-4, -1.78462E-4, -7.04117E-5, 4.32768E-5, 1.58002E-4, 2.69003E-4, 3.71559E-4, 4.61181E-4, 5.33813E-4, 5.86005E-4, 6.15078E-4, 6.19249E-4, 5.97735E-4, 5.50801E-4, 4.79783E-4, 3.87053E-4, 2.75948E-4, 1.50652E-4, 1.6043E-5, -1.22494E-4, -2.5928E-4, -3.88572E-4, -5.048E-4, -6.02806E-4, -6.78068E-4, -7.26902E-4, -7.46639E-4, -7.3576E-4, -6.93986E-4, -6.22318E-4, -5.2303E-4, -3.99598E-4, -2.56591E-4, -9.94981E-5, 6.54703E-5, 2.31631E-4, 3.92092E-4, 5.40036E-4, 6.69007E-4, 7.73187E-4, 8.47653E-4, 8.88607E-4, 8.93555E-4, 8.61444E-4, 7.92744E-4, 6.89455E-4, 5.55068E-4, 3.94449E-4, 2.1367E-4, 1.9785E-5, -1.79442E-4, -3.75845E-4, -5.61183E-4, -7.27486E-4, -8.67391E-4, -9.74462E-4, -0.00104348, -0.00107069, -0.00105399, -9.93048E-4, -8.89383E-4, -7.46314E-4, -5.68878E-4, -3.6366E-4, -1.38554E-4, 9.75391E-5, 3.35052E-4, 5.6414E-4, 7.75079E-4, 9.58677E-4, 0.00110667, 0.00121207, 0.00126951, 0.0012755, 0.00122859, 0.00112951, 9.81172E-4, 7.88601E-4, 5.58778E-4, 3.00394E-4, 2.35286E-5, -2.60737E-4, -5.40758E-4, -8.04795E-4, -0.0010415, -0.0012404, -0.00139235, -0.00148993, -0.00152783, -0.00150307, -0.00141522, -0.00126646, -0.00106153, -8.07641E-4, -5.14192E-4, -1.92446E-4, 1.44894E-4, 4.84179E-4, 8.11358E-4, 0.00111255, 0.00137463, 0.00158578, 0.00173601, 0.00181764, 0.00182563, 0.00175791, 0.00161545, 0.00140239, 0.00112588, 7.9588E-4, 4.24794E-4, 2.70414E-5, -3.81505E-4, -7.84141E-4, -0.001164, -0.00150475, -0.00179128, -0.00201036, -0.00215121, -0.00220606, -0.00217049, -0.00204371, -0.00182871, -0.00153219, -0.00116441, -7.38835E-4, -2.71661E-4, 2.1878E-4, 7.12724E-4, 0.00118976, 0.00162964, 0.00201316, 0.00232291, 0.00254411, 0.00266525, 0.00267866, 0.00258091, 0.00237308, 0.00206085, 0.00165434, 0.00116787, 6.19435E-4, 3.0085E-5, -5.76858E-4, -0.0011767, -0.00174437, -0.00225539, -0.00268698, -0.00301895, -0.00323467, -0.00332183, -0.00327306, -0.00308638, -0.00276546, -0.00231959, -0.00176349, -0.00111688, -4.03798E-4, 3.48252E-4, 0.00110931, 0.00184811, 0.00253333, 0.00313487, 0.00362507, 0.00398001, 0.00418052, 0.00421314, 0.00407087, 0.00375364, 0.00326856, 0.00262989, 0.00185863, 9.81988E-4, 3.24401E-5, -9.53391E-4, -0.00193606, -0.00287481, -0.00372915, -0.00446049, -0.0050338, -0.00541911, -0.00559292, -0.0055394, -0.00525131, -0.00473064, -0.00398887, -0.00304693, -0.00193477, -6.90514E-4, 6.40686E-4, 0.00200811, 0.00335722, 0.00463166, 0.00577533, 0.00673459, 0.00746048, 0.00791079, 0.00805206, 0.00786122, 0.00732706, 0.00645119, 0.00524871, 0.00374829, 0.0019919, 3.39299E-5, -0.00206002, -0.00421493, -0.00634843, -0.00837327, -0.0102, -0.01174, -0.0129082, -0.0136262, -0.0138249, -0.0134475, -0.0124515, -0.0108105, -0.00851603, -0.00557815, -0.00202594, 0.00209271, 0.00671199, 0.0117495, 0.0171084, 0.0226794, 0.028344, 0.0339778, 0.0394534, 0.0446444, 0.0494288, 0.0536926, 0.0573331, 0.0602617, 0.0624066, 0.0637152, 0.064155, 0.0637152, 0.0624066, 0.0602617, 0.0573331, 0.0536926, 0.0494288, 0.0446444, 0.0394534, 0.0339778, 0.028344, 0.0226794, 0.0171084, 0.0117495, 0.00671199, 0.00209271, -0.00202594, -0.00557815, -0.00851603, -0.0108105, -0.0124515, -0.0134475, -0.0138249, -0.0136262, -0.0129082, -0.01174, -0.0102, -0.00837327, -0.00634843, -0.00421493, -0.00206002, 3.39299E-5, 0.0019919, 0.00374829, 0.00524871, 0.00645119, 0.00732706, 0.00786122, 0.00805206, 0.00791079, 0.00746048, 0.00673459, 0.00577533, 0.00463166, 0.00335722, 0.00200811, 6.40686E-4, -6.90514E-4, -0.00193477, -0.00304693, -0.00398887, -0.00473064, -0.00525131, -0.0055394, -0.00559292, -0.00541911, -0.0050338, -0.00446049, -0.00372915, -0.00287481, -0.00193606, -9.53391E-4, 3.24401E-5, 9.81988E-4, 0.00185863, 0.00262989, 0.00326856, 0.00375364, 0.00407087, 0.00421314, 0.00418052, 0.00398001, 0.00362507, 0.00313487, 0.00253333, 0.00184811, 0.00110931, 3.48252E-4, -4.03798E-4, -0.00111688, -0.00176349, -0.00231959, -0.00276546, -0.00308638, -0.00327306, -0.00332183, -0.00323467, -0.00301895, -0.00268698, -0.00225539, -0.00174437, -0.0011767, -5.76858E-4, 3.0085E-5, 6.19435E-4, 0.00116787, 0.00165434, 0.00206085, 0.00237308, 0.00258091, 0.00267866, 0.00266525, 0.00254411, 0.00232291, 0.00201316, 0.00162964, 0.00118976, 7.12724E-4, 2.1878E-4, -2.71661E-4, -7.38835E-4, -0.00116441, -0.00153219, -0.00182871, -0.00204371, -0.00217049, -0.00220606, -0.00215121, -0.00201036, -0.00179128, -0.00150475, -0.001164, -7.84141E-4, -3.81505E-4, 2.70414E-5, 4.24794E-4, 7.9588E-4, 0.00112588, 0.00140239, 0.00161545, 0.00175791, 0.00182563, 0.00181764, 0.00173601, 0.00158578, 0.00137463, 0.00111255, 8.11358E-4, 4.84179E-4, 1.44894E-4, -1.92446E-4, -5.14192E-4, -8.07641E-4, -0.00106153, -0.00126646, -0.00141522, -0.00150307, -0.00152783, -0.00148993, -0.00139235, -0.0012404, -0.0010415, -8.04795E-4, -5.40758E-4, -2.60737E-4, 2.35286E-5, 3.00394E-4, 5.58778E-4, 7.88601E-4, 9.81172E-4, 0.00112951, 0.00122859, 0.0012755, 0.00126951, 0.00121207, 0.00110667, 9.58677E-4, 7.75079E-4, 5.6414E-4, 3.35052E-4, 9.75391E-5, -1.38554E-4, -3.6366E-4, -5.68878E-4, -7.46314E-4, -8.89383E-4, -9.93048E-4, -0.00105399, -0.00107069, -0.00104348, -9.74462E-4, -8.67391E-4, -7.27486E-4, -5.61183E-4, -3.75845E-4, -1.79442E-4, 1.9785E-5, 2.1367E-4, 3.94449E-4, 5.55068E-4, 6.89455E-4, 7.92744E-4, 8.61444E-4, 8.93555E-4, 8.88607E-4, 8.47653E-4, 7.73187E-4, 6.69007E-4, 5.40036E-4, 3.92092E-4, 2.31631E-4, 6.54703E-5, -9.94981E-5, -2.56591E-4, -3.99598E-4, -5.2303E-4, -6.22318E-4, -6.93986E-4, -7.3576E-4, -7.46639E-4, -7.26902E-4, -6.78068E-4, -6.02806E-4, -5.048E-4, -3.88572E-4, -2.5928E-4, -1.22494E-4, 1.6043E-5, 1.50652E-4, 2.75948E-4, 3.87053E-4, 4.79783E-4, 5.50801E-4, 5.97735E-4, 6.19249E-4, 6.15078E-4, 5.86005E-4, 5.33813E-4, 4.61181E-4, 3.71559E-4, 2.69003E-4, 1.58002E-4, 4.32768E-5, -7.04117E-5, -1.78462E-4, -2.76612E-4, -3.61111E-4, -4.28852E-4, -4.7749E-4, -5.05513E-4, -5.12288E-4, -4.98065E-4, -4.63942E-4, -4.118E-4, -3.44209E-4, -2.64305E-4, -1.75649E-4, -8.20668E-5, 1.25074E-5, 1.04199E-4, 1.89348E-4, 2.64653E-4, 3.27296E-4, 3.75049E-4, 4.06345E-4, 4.20328E-4, 4.16871E-4, 3.96561E-4, 3.60655E-4, 3.11016E-4, 2.50019E-4, 1.8044E-4, 1.05334E-4, 2.79015E-5, -4.8644E-5, -1.21209E-4, -1.86944E-4, -2.4335E-4, -2.88376E-4, -3.20488E-4, -3.3872E-4, -3.42696E-4, -3.32633E-4, -3.09314E-4, -2.74041E-4, -2.28571E-4, -1.75029E-4, -1.1581E-4, -5.3481E-5, 9.33741E-6, 7.00735E-5, 1.26311E-4, 1.75881E-4, 2.16949E-4, 2.48073E-4, 2.68261E-4, 2.76991E-4, 2.74223E-4, 2.6039E-4, 2.36361E-4, 2.034E-4, 1.63097E-4, 1.17301E-4, 6.80297E-5, 1.73888E-5, -3.2521E-5, -7.96875E-5, -1.22268E-4, -1.58659E-4, -1.87554E-4, -2.07994E-4, -2.19387E-4, -2.21531E-4, -2.14607E-4, -1.9916E-4, -1.76068E-4, -1.46493E-4, -1.11833E-4, -7.36463E-5, -3.35942E-5, 6.63672E-6, 4.54024E-5, 8.11672E-5, 1.12564E-4, 1.38444E-4, 1.57919E-4, 1.70389E-4, 1.75557E-4, 1.73436E-4, 1.64332E-4, 1.48832E-4, 1.27762E-4, 1.02153E-4, 7.31874E-5, 4.21504E-5, 1.03708E-5, -2.08331E-5, -5.02078E-5, -7.66139E-5, -9.90687E-5, -1.16782E-4, -1.29183E-4, -1.35936E-4, -1.36945E-4, -1.32355E-4, -1.22532E-4, -1.08047E-4, -8.96409E-5, -6.81921E-5, -4.46746E-5, -2.01149E-5, 4.45135E-6, 2.80223E-5, 4.96701E-5, 6.85762E-5, 8.40613E-5, 9.56091E-5, 1.02881E-4, 1.05726E-4, 1.04179E-4, 9.84511E-5, 8.89208E-5, 7.6107E-5, 6.0646E-5, 4.32603E-5, 2.4726E-5, 5.8394E-6, -1.26166E-5, -2.99042E-5, -4.53596E-5, -5.84174E-5, -6.86304E-5, -7.5684E-5, -7.94043E-5, -7.97603E-5, -7.68599E-5, -7.09404E-5, -6.23545E-5, -5.15509E-5, -3.90529E-5, -2.5434E-5, -1.12922E-5, 2.77535E-6, 1.61969E-5, 2.84488E-5, 3.90751E-5, 4.7704E-5, 5.406E-5, 5.79712E-5, 5.93728E-5, 5.83058E-5, 5.49107E-5, 4.94182E-5, 4.21361E-5, 3.34335E-5, 2.37233E-5, 1.34431E-5, 3.03662E-6, -7.06546E-6, -1.64623E-5, -2.47986E-5, -3.17772E-5, -3.71691E-5, -4.08201E-5, -4.26543E-5, -4.26737E-5, -4.0955E-5, -3.76432E-5, -3.29429E-5, -2.7107E-5, -2.04243E-5, -1.32062E-5, -5.77215E-6, 1.56325E-6, 8.5036E-6, 1.4782E-5, 2.0171E-5, 2.44899E-5, 2.7611E-5, 2.94615E-5, 3.00248E-5, 2.93384E-5, 2.74896E-5, 2.46099E-5, 2.08674E-5, 1.64577E-5, 1.15948E-5, 6.50109E-6, 1.39782E-6, -3.50439E-6, -8.0137E-6, -1.19642E-5, -1.52215E-5, -1.76871E-5, -1.93004E-5, -2.00395E-5, -1.99201E-5, -1.89929E-5, -1.73398E-5, -1.50687E-5, -1.23073E-5, -9.19725E-6, -5.88682E-6, -2.52458E-6, 0.0, 3.79731E-6, 6.51244E-6, 8.79918E-6, 1.05877E-5, 1.18334E-5, 1.25172E-5, 1.26449E-5, 1.22456E-5, 1.13688E-5, 1.00815E-5, 8.4637E-6, 6.60467E-6, 4.59817E-6, 2.53827E-6, 0.0, -1.3884E-6, -3.10043E-6, -4.56196E-6, -5.72888E-6, -6.57284E-6, -7.0815E-6, -7.2579E-6, -7.11936E-6, -6.69564E-6, -6.0268E-6, -5.16062E-6, -4.14996E-6, -3.04996E-6, -1.91541E-6, 0.0, 0.0, 1.20237E-6, 2.01338E-6, 2.66409E-6, 3.14043E-6, 3.4375E-6, 3.55902E-6, 3.51644E-6, 3.32777E-6, 3.01612E-6, 2.60813E-6, 2.13243E-6, 1.618E-6, 1.09279E-6, 0.0, 0.0, 0.0, 0.0, 0.0, -1.12683E-6, -1.24088E-6, -1.28055E-6, -1.2541E-6, -1.17223E-6, -1.0473E-6, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "number_of_sets" value="2"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "output_full_bit_width" value="62"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "output_full_bits_right_of_binary_point" value="50"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "coefficient_reload_bit_width" value="24"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "logic_cell" value="875"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "m512" value="0"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "m4k" value="0"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "m144k" value="0"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "m9k" value="18"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "m20k" value="0"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "mlab" value="0"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "megaram" value="0"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "dsp_block" value="1"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "input_clock_period" value="512"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "output_clock_period" value="512"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "throughput" value="512"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "memory_units" value="1"  type="INTEGER"  enable="1" />
-- Retrieval info:      <PRIVATE name = "set_2" value="Low Pass Set, Floating, Low Pass, Blackman, 1023, 48000.0, 250.0, 3750000.0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.07314E-6, 1.24211E-6, 1.42356E-6, 1.61771E-6, 1.82475E-6, 2.04484E-6, 2.27811E-6, 2.52465E-6, 2.78452E-6, 3.05775E-6, 3.34431E-6, 3.64416E-6, 3.9572E-6, 4.2833E-6, 4.62227E-6, 4.9739E-6, 5.33792E-6, 5.71403E-6, 6.10186E-6, 6.50102E-6, 6.91106E-6, 7.33148E-6, 7.76174E-6, 8.20125E-6, 8.64936E-6, 9.10538E-6, 9.56858E-6, 1.00382E-5, 1.05133E-5, 1.09931E-5, 1.14766E-5, 1.19629E-5, 1.24509E-5, 1.29395E-5, 1.34276E-5, 1.39141E-5, 1.43977E-5, 1.48772E-5, 1.53512E-5, 1.58185E-5, 1.62776E-5, 1.67271E-5, 1.71655E-5, 1.75915E-5, 1.80033E-5, 1.83995E-5, 1.87786E-5, 1.91387E-5, 1.94784E-5, 1.9796E-5, 2.00897E-5, 2.03579E-5, 2.05989E-5, 2.08109E-5, 2.09921E-5, 2.11409E-5, 2.12554E-5, 2.1334E-5, 2.13749E-5, 2.13763E-5, 2.13366E-5, 2.12539E-5, 2.11266E-5, 2.0953E-5, 2.07316E-5, 2.04605E-5, 2.01383E-5, 1.97634E-5, 1.93343E-5, 1.88495E-5, 1.83075E-5, 1.77071E-5, 1.7047E-5, 1.63257E-5, 1.55423E-5, 1.46955E-5, 1.37843E-5, 1.28078E-5, 1.17651E-5, 1.06553E-5, 9.47787E-6, 8.23208E-6, 6.91745E-6, 5.53358E-6, 4.08017E-6, 2.55703E-6, 0.0, 0.0, -2.43097E-6, -4.2326E-6, -6.10312E-6, -8.04199E-6, -1.00485E-5, -1.21219E-5, -1.42611E-5, -1.64651E-5, -1.87326E-5, -2.10623E-5, -2.34525E-5, -2.59015E-5, -2.84075E-5, -3.09685E-5, -3.35824E-5, -3.62467E-5, -3.8959E-5, -4.17166E-5, -4.45168E-5, -4.73565E-5, -5.02327E-5, -5.3142E-5, -5.60809E-5, -5.9046E-5, -6.20333E-5, -6.50391E-5, -6.80592E-5, -7.10893E-5, -7.41252E-5, -7.71622E-5, -8.01957E-5, -8.32209E-5, -8.62329E-5, -8.92265E-5, -9.21966E-5, -9.51378E-5, -9.80447E-5, -1.00912E-4, -1.03733E-4, -1.06503E-4, -1.09216E-4, -1.11865E-4, -1.14446E-4, -1.16951E-4, -1.19374E-4, -1.21709E-4, -1.23951E-4, -1.26092E-4, -1.28125E-4, -1.30046E-4, -1.31847E-4, -1.33521E-4, -1.35063E-4, -1.36466E-4, -1.37723E-4, -1.38829E-4, -1.39776E-4, -1.40558E-4, -1.4117E-4, -1.41605E-4, -1.41857E-4, -1.4192E-4, -1.41788E-4, -1.41456E-4, -1.40918E-4, -1.40167E-4, -1.392E-4, -1.38011E-4, -1.36595E-4, -1.34948E-4, -1.33063E-4, -1.30939E-4, -1.28569E-4, -1.25951E-4, -1.23081E-4, -1.19955E-4, -1.16571E-4, -1.12925E-4, -1.09015E-4, -1.0484E-4, -1.00396E-4, -9.56836E-5, -9.07004E-5, -8.54459E-5, -7.99196E-5, -7.41217E-5, -6.80523E-5, -6.17123E-5, -5.51027E-5, -4.82251E-5, -4.10815E-5, -3.36743E-5, -2.60063E-5, -1.80808E-5, -9.90163E-6, -1.47284E-6, 7.20086E-6, 1.61144E-5, 2.52621E-5, 3.46379E-5, 4.42354E-5, 5.40475E-5, 6.40667E-5, 7.42851E-5, 8.46941E-5, 9.52848E-5, 1.06048E-4, 1.16973E-4, 1.28051E-4, 1.39269E-4, 1.50618E-4, 1.62084E-4, 1.73656E-4, 1.85322E-4, 1.97067E-4, 2.08879E-4, 2.20743E-4, 2.32644E-4, 2.44569E-4, 2.56502E-4, 2.68426E-4, 2.80327E-4, 2.92187E-4, 3.0399E-4, 3.15719E-4, 3.27357E-4, 3.38885E-4, 3.50286E-4, 3.61542E-4, 3.72633E-4, 3.83542E-4, 3.94249E-4, 4.04735E-4, 4.14981E-4, 4.24968E-4, 4.34675E-4, 4.44084E-4, 4.53174E-4, 4.61926E-4, 4.7032E-4, 4.78336E-4, 4.85954E-4, 4.93154E-4, 4.99918E-4, 5.06225E-4, 5.12056E-4, 5.17391E-4, 5.22212E-4, 5.26501E-4, 5.30237E-4, 5.33404E-4, 5.35983E-4, 5.37956E-4, 5.39307E-4, 5.40019E-4, 5.40074E-4, 5.39458E-4, 5.38155E-4, 5.3615E-4, 5.33428E-4, 5.29977E-4, 5.25782E-4, 5.20832E-4, 5.15115E-4, 5.0862E-4, 5.01336E-4, 4.93254E-4, 4.84365E-4, 4.74661E-4, 4.64136E-4, 4.52784E-4, 4.40598E-4, 4.27575E-4, 4.13711E-4, 3.99004E-4, 3.83453E-4, 3.67057E-4, 3.49818E-4, 3.31736E-4, 3.12815E-4, 2.9306E-4, 2.72474E-4, 2.51065E-4, 2.2884E-4, 2.05807E-4, 1.81977E-4, 1.5736E-4, 1.31969E-4, 1.05818E-4, 7.892E-5, 5.12925E-5, 2.29523E-5, -6.08216E-6, -3.57911E-5, -6.61535E-5, -9.71472E-5, -1.28749E-4, -1.60933E-4, -1.93674E-4, -2.26945E-4, -2.60717E-4, -2.94961E-4, -3.29645E-4, -3.64737E-4, -4.00205E-4, -4.36013E-4, -4.72126E-4, -5.08508E-4, -5.45119E-4, -5.81922E-4, -6.18876E-4, -6.5594E-4, -6.93073E-4, -7.3023E-4, -7.67368E-4, -8.04442E-4, -8.41406E-4, -8.78213E-4, -9.14815E-4, -9.51165E-4, -9.87214E-4, -0.00102291, -0.00105821, -0.00109305, -0.00112738, -0.00116116, -0.00119433, -0.00122684, -0.00125863, -0.00128965, -0.00131985, -0.00134917, -0.00137756, -0.00140496, -0.00143131, -0.00145657, -0.00148068, -0.00150358, -0.00152523, -0.00154555, -0.00156451, -0.00158205, -0.00159811, -0.00161265, -0.0016256, -0.00163693, -0.00164657, -0.00165448, -0.00166061, -0.00166491, -0.00166733, -0.00166783, -0.00166635, -0.00166287, -0.00165732, -0.00164967, -0.00163988, -0.00162791, -0.00161373, -0.00159728, -0.00157854, -0.00155748, -0.00153406, -0.00150825, -0.00148002, -0.00144935, -0.0014162, -0.00138056, -0.00134241, -0.00130172, -0.00125848, -0.00121266, -0.00116427, -0.00111327, -0.00105968, -0.00100347, -9.44645E-4, -8.83199E-4, -8.19131E-4, -7.52444E-4, -6.83141E-4, -6.11227E-4, -5.36713E-4, -4.59607E-4, -3.79923E-4, -2.97677E-4, -2.12887E-4, -1.25573E-4, -3.57565E-5, 5.65361E-5, 1.51278E-4, 2.48439E-4, 3.47987E-4, 4.49887E-4, 5.54103E-4, 6.60594E-4, 7.69321E-4, 8.80237E-4, 9.93297E-4, 0.00110845, 0.00122565, 0.00134485, 0.00146597, 0.00158898, 0.0017138, 0.00184039, 0.00196866, 0.00209857, 0.00223003, 0.00236298, 0.00249736, 0.00263307, 0.00277006, 0.00290824, 0.00304754, 0.00318787, 0.00332915, 0.00347131, 0.00361424, 0.00375788, 0.00390213, 0.00404691, 0.00419211, 0.00433767, 0.00448347, 0.00462944, 0.00477547, 0.00492147, 0.00506736, 0.00521302, 0.00535838, 0.00550332, 0.00564776, 0.0057916, 0.00593474, 0.00607709, 0.00621854, 0.00635901, 0.00649839, 0.00663659, 0.00677351, 0.00690906, 0.00704314, 0.00717566, 0.00730652, 0.00743564, 0.00756291, 0.00768826, 0.00781158, 0.00793279, 0.00805181, 0.00816855, 0.00828291, 0.00839483, 0.00850422, 0.00861099, 0.00871507, 0.00881638, 0.00891486, 0.00901041, 0.00910299, 0.0091925, 0.0092789, 0.00936211, 0.00944208, 0.00951874, 0.00959203, 0.00966191, 0.00972831, 0.00979119, 0.0098505, 0.0099062, 0.00995824, 0.0100066, 0.0100512, 0.0100921, 0.0101291, 0.0101623, 0.0101917, 0.0102172, 0.0102388, 0.0102566, 0.0102703, 0.0102802, 0.0102861, 0.0102881, 0.0102861, 0.0102802, 0.0102703, 0.0102566, 0.0102388, 0.0102172, 0.0101917, 0.0101623, 0.0101291, 0.0100921, 0.0100512, 0.0100066, 0.00995824, 0.0099062, 0.0098505, 0.00979119, 0.00972831, 0.00966191, 0.00959203, 0.00951874, 0.00944208, 0.00936211, 0.0092789, 0.0091925, 0.00910299, 0.00901041, 0.00891486, 0.00881638, 0.00871507, 0.00861099, 0.00850422, 0.00839483, 0.00828291, 0.00816855, 0.00805181, 0.00793279, 0.00781158, 0.00768826, 0.00756291, 0.00743564, 0.00730652, 0.00717566, 0.00704314, 0.00690906, 0.00677351, 0.00663659, 0.00649839, 0.00635901, 0.00621854, 0.00607709, 0.00593474, 0.0057916, 0.00564776, 0.00550332, 0.00535838, 0.00521302, 0.00506736, 0.00492147, 0.00477547, 0.00462944, 0.00448347, 0.00433767, 0.00419211, 0.00404691, 0.00390213, 0.00375788, 0.00361424, 0.00347131, 0.00332915, 0.00318787, 0.00304754, 0.00290824, 0.00277006, 0.00263307, 0.00249736, 0.00236298, 0.00223003, 0.00209857, 0.00196866, 0.00184039, 0.0017138, 0.00158898, 0.00146597, 0.00134485, 0.00122565, 0.00110845, 9.93297E-4, 8.80237E-4, 7.69321E-4, 6.60594E-4, 5.54103E-4, 4.49887E-4, 3.47987E-4, 2.48439E-4, 1.51278E-4, 5.65361E-5, -3.57565E-5, -1.25573E-4, -2.12887E-4, -2.97677E-4, -3.79923E-4, -4.59607E-4, -5.36713E-4, -6.11227E-4, -6.83141E-4, -7.52444E-4, -8.19131E-4, -8.83199E-4, -9.44645E-4, -0.00100347, -0.00105968, -0.00111327, -0.00116427, -0.00121266, -0.00125848, -0.00130172, -0.00134241, -0.00138056, -0.0014162, -0.00144935, -0.00148002, -0.00150825, -0.00153406, -0.00155748, -0.00157854, -0.00159728, -0.00161373, -0.00162791, -0.00163988, -0.00164967, -0.00165732, -0.00166287, -0.00166635, -0.00166783, -0.00166733, -0.00166491, -0.00166061, -0.00165448, -0.00164657, -0.00163693, -0.0016256, -0.00161265, -0.00159811, -0.00158205, -0.00156451, -0.00154555, -0.00152523, -0.00150358, -0.00148068, -0.00145657, -0.00143131, -0.00140496, -0.00137756, -0.00134917, -0.00131985, -0.00128965, -0.00125863, -0.00122684, -0.00119433, -0.00116116, -0.00112738, -0.00109305, -0.00105821, -0.00102291, -9.87214E-4, -9.51165E-4, -9.14815E-4, -8.78213E-4, -8.41406E-4, -8.04442E-4, -7.67368E-4, -7.3023E-4, -6.93073E-4, -6.5594E-4, -6.18876E-4, -5.81922E-4, -5.45119E-4, -5.08508E-4, -4.72126E-4, -4.36013E-4, -4.00205E-4, -3.64737E-4, -3.29645E-4, -2.94961E-4, -2.60717E-4, -2.26945E-4, -1.93674E-4, -1.60933E-4, -1.28749E-4, -9.71472E-5, -6.61535E-5, -3.57911E-5, -6.08216E-6, 2.29523E-5, 5.12925E-5, 7.892E-5, 1.05818E-4, 1.31969E-4, 1.5736E-4, 1.81977E-4, 2.05807E-4, 2.2884E-4, 2.51065E-4, 2.72474E-4, 2.9306E-4, 3.12815E-4, 3.31736E-4, 3.49818E-4, 3.67057E-4, 3.83453E-4, 3.99004E-4, 4.13711E-4, 4.27575E-4, 4.40598E-4, 4.52784E-4, 4.64136E-4, 4.74661E-4, 4.84365E-4, 4.93254E-4, 5.01336E-4, 5.0862E-4, 5.15115E-4, 5.20832E-4, 5.25782E-4, 5.29977E-4, 5.33428E-4, 5.3615E-4, 5.38155E-4, 5.39458E-4, 5.40074E-4, 5.40019E-4, 5.39307E-4, 5.37956E-4, 5.35983E-4, 5.33404E-4, 5.30237E-4, 5.26501E-4, 5.22212E-4, 5.17391E-4, 5.12056E-4, 5.06225E-4, 4.99918E-4, 4.93154E-4, 4.85954E-4, 4.78336E-4, 4.7032E-4, 4.61926E-4, 4.53174E-4, 4.44084E-4, 4.34675E-4, 4.24968E-4, 4.14981E-4, 4.04735E-4, 3.94249E-4, 3.83542E-4, 3.72633E-4, 3.61542E-4, 3.50286E-4, 3.38885E-4, 3.27357E-4, 3.15719E-4, 3.0399E-4, 2.92187E-4, 2.80327E-4, 2.68426E-4, 2.56502E-4, 2.44569E-4, 2.32644E-4, 2.20743E-4, 2.08879E-4, 1.97067E-4, 1.85322E-4, 1.73656E-4, 1.62084E-4, 1.50618E-4, 1.39269E-4, 1.28051E-4, 1.16973E-4, 1.06048E-4, 9.52848E-5, 8.46941E-5, 7.42851E-5, 6.40667E-5, 5.40475E-5, 4.42354E-5, 3.46379E-5, 2.52621E-5, 1.61144E-5, 7.20086E-6, -1.47284E-6, -9.90163E-6, -1.80808E-5, -2.60063E-5, -3.36743E-5, -4.10815E-5, -4.82251E-5, -5.51027E-5, -6.17123E-5, -6.80523E-5, -7.41217E-5, -7.99196E-5, -8.54459E-5, -9.07004E-5, -9.56836E-5, -1.00396E-4, -1.0484E-4, -1.09015E-4, -1.12925E-4, -1.16571E-4, -1.19955E-4, -1.23081E-4, -1.25951E-4, -1.28569E-4, -1.30939E-4, -1.33063E-4, -1.34948E-4, -1.36595E-4, -1.38011E-4, -1.392E-4, -1.40167E-4, -1.40918E-4, -1.41456E-4, -1.41788E-4, -1.4192E-4, -1.41857E-4, -1.41605E-4, -1.4117E-4, -1.40558E-4, -1.39776E-4, -1.38829E-4, -1.37723E-4, -1.36466E-4, -1.35063E-4, -1.33521E-4, -1.31847E-4, -1.30046E-4, -1.28125E-4, -1.26092E-4, -1.23951E-4, -1.21709E-4, -1.19374E-4, -1.16951E-4, -1.14446E-4, -1.11865E-4, -1.09216E-4, -1.06503E-4, -1.03733E-4, -1.00912E-4, -9.80447E-5, -9.51378E-5, -9.21966E-5, -8.92265E-5, -8.62329E-5, -8.32209E-5, -8.01957E-5, -7.71622E-5, -7.41252E-5, -7.10893E-5, -6.80592E-5, -6.50391E-5, -6.20333E-5, -5.9046E-5, -5.60809E-5, -5.3142E-5, -5.02327E-5, -4.73565E-5, -4.45168E-5, -4.17166E-5, -3.8959E-5, -3.62467E-5, -3.35824E-5, -3.09685E-5, -2.84075E-5, -2.59015E-5, -2.34525E-5, -2.10623E-5, -1.87326E-5, -1.64651E-5, -1.42611E-5, -1.21219E-5, -1.00485E-5, -8.04199E-6, -6.10312E-6, -4.2326E-6, -2.43097E-6, 0.0, 0.0, 2.55703E-6, 4.08017E-6, 5.53358E-6, 6.91745E-6, 8.23208E-6, 9.47787E-6, 1.06553E-5, 1.17651E-5, 1.28078E-5, 1.37843E-5, 1.46955E-5, 1.55423E-5, 1.63257E-5, 1.7047E-5, 1.77071E-5, 1.83075E-5, 1.88495E-5, 1.93343E-5, 1.97634E-5, 2.01383E-5, 2.04605E-5, 2.07316E-5, 2.0953E-5, 2.11266E-5, 2.12539E-5, 2.13366E-5, 2.13763E-5, 2.13749E-5, 2.1334E-5, 2.12554E-5, 2.11409E-5, 2.09921E-5, 2.08109E-5, 2.05989E-5, 2.03579E-5, 2.00897E-5, 1.9796E-5, 1.94784E-5, 1.91387E-5, 1.87786E-5, 1.83995E-5, 1.80033E-5, 1.75915E-5, 1.71655E-5, 1.67271E-5, 1.62776E-5, 1.58185E-5, 1.53512E-5, 1.48772E-5, 1.43977E-5, 1.39141E-5, 1.34276E-5, 1.29395E-5, 1.24509E-5, 1.19629E-5, 1.14766E-5, 1.09931E-5, 1.05133E-5, 1.00382E-5, 9.56858E-6, 9.10538E-6, 8.64936E-6, 8.20125E-6, 7.76174E-6, 7.33148E-6, 6.91106E-6, 6.50102E-6, 6.10186E-6, 5.71403E-6, 5.33792E-6, 4.9739E-6, 4.62227E-6, 4.2833E-6, 3.9572E-6, 3.64416E-6, 3.34431E-6, 3.05775E-6, 2.78452E-6, 2.52465E-6, 2.27811E-6, 2.04484E-6, 1.82475E-6, 1.61771E-6, 1.42356E-6, 1.24211E-6, 1.07314E-6, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0"  type="STRING"  enable="1" />
-- Retrieval info:     </NAMESPACE>
-- Retrieval info:     <NAMESPACE name = "simgen_enable">
-- Retrieval info:      <PRIVATE name = "matlab_enable" value="1"  type="BOOLEAN"  enable="1" />
-- Retrieval info:      <PRIVATE name = "testbench_enable" value="1"  type="BOOLEAN"  enable="1" />
-- Retrieval info:      <PRIVATE name = "testbench_simulation_clock_period" value="10.0"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "language" value="VHDL"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "enabled" value="0"  type="BOOLEAN"  enable="1" />
-- Retrieval info:     </NAMESPACE>
-- Retrieval info:     <NAMESPACE name = "simgen">
-- Retrieval info:      <PRIVATE name = "filename" value="cicdec0.vho"  type="STRING"  enable="1" />
-- Retrieval info:     </NAMESPACE>
-- Retrieval info:     <NAMESPACE name = "quartus_settings">
-- Retrieval info:      <PRIVATE name = "DEVICE" value="EP4CE22E22I7"  type="STRING"  enable="1" />
-- Retrieval info:      <PRIVATE name = "FAMILY" value="Cyclone IV E"  type="STRING"  enable="1" />
-- Retrieval info:     </NAMESPACE>
-- Retrieval info:     <NAMESPACE name = "serializer"/>
-- Retrieval info:    </PRIVATES>
-- Retrieval info:    <FILES/>
-- Retrieval info:    <PORTS/>
-- Retrieval info:    <LIBRARIES/>
-- Retrieval info:   </STATIC_SECTION>
-- Retrieval info:  </NETLIST_SECTION>
-- Retrieval info: </MEGACORE>
-- =========================================================
