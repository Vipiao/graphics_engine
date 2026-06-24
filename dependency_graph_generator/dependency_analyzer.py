#!/usr/bin/env python3

import os
import sys
import re
import argparse
from pathlib import Path
from collections import defaultdict
import math
import xml.sax.saxutils as saxutils

def find_cpp_files(directory, skip_folders=None):
    """Find all .cpp and .h files recursively in the given directory, skipping specified folders."""
    if skip_folders is None:
        skip_folders = []
    
    cpp_files = []
    base_path = Path(directory).resolve()
    
    for root, dirs, files in os.walk(directory):
        # Convert current root to Path for easier manipulation
        current_path = Path(root).resolve()
        
        # Check if current directory or any parent should be skipped
        try:
            relative_path = current_path.relative_to(base_path)
            should_skip = False
            
            for skip_folder in skip_folders:
                skip_path = Path(skip_folder)
                # Check if the relative path starts with any skip folder
                try:
                    relative_path.relative_to(skip_path)
                    should_skip = True
                    break
                except ValueError:
                    # Check if any part of the path matches the skip folder
                    if skip_path.name in relative_path.parts:
                        should_skip = True
                        break
            
            if should_skip:
                # Skip this directory and all subdirectories
                dirs.clear()
                continue
                
        except ValueError:
            # current_path is not relative to base_path, shouldn't happen but skip to be safe
            continue
        
        # Remove skip folders from dirs to prevent os.walk from entering them
        dirs[:] = [d for d in dirs if d not in [Path(skip).name for skip in skip_folders]]
        
        for file in files:
            if file.endswith(('.cpp', '.h', '.hpp', '.cc', '.cxx')):
                cpp_files.append(os.path.join(root, file))
    
    return cpp_files

def extract_filename_without_extension(filepath):
    """Extract filename without extension."""
    return Path(filepath).stem

def parse_includes(file_content):
    """Parse #include statements from file content."""
    includes = []
    # Match both #include "file.h" and #include <file.h>
    include_pattern = r'#include\s*[<"]([^>"]+)[>"]'
    matches = re.findall(include_pattern, file_content)
    
    for match in matches:
        # Extract just the filename without path and extension
        filename = Path(match).stem
        includes.append(filename)
    
    return includes

def parse_forward_declarations(file_content):
    """Parse forward declarations (class/struct) from file content."""
    forward_decls = []
    # Match forward declarations like "class ClassName;" or "struct StructName;"
    # Handle optional namespace qualifiers
    class_pattern = r'(?:^|\s)(?:class|struct)\s+(?:\w+::)*(\w+)\s*;'
    matches = re.findall(class_pattern, file_content, re.MULTILINE)
    
    forward_decls.extend(matches)
    return forward_decls

def sanitize_node_id(name):
    """Sanitize node name to be valid XML ID."""
    # Replace problematic characters with underscores
    sanitized = re.sub(r'[^a-zA-Z0-9_]', '_', name)
    # Ensure it starts with a letter
    if sanitized and sanitized[0].isdigit():
        sanitized = 'n' + sanitized
    return sanitized or 'unknown'

def analyze_dependencies(directory, skip_folders=None):
    """Analyze dependencies between C++ files."""
    cpp_files = find_cpp_files(directory, skip_folders)
    dependencies = defaultdict(set)
    all_nodes = set()
    
    # First pass: collect all possible nodes
    for file_path in cpp_files:
        node_name = extract_filename_without_extension(file_path)
        all_nodes.add(node_name)
    
    # Second pass: find dependencies
    for file_path in cpp_files:
        try:
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
        except Exception as e:
            print(f"Warning: Could not read {file_path}: {e}")
            continue
            
        source_node = extract_filename_without_extension(file_path)
        
        # Find includes
        includes = parse_includes(content)
        for include in includes:
            if include in all_nodes and include != source_node:
                dependencies[source_node].add(include)
        
        # Find forward declarations
        forward_decls = parse_forward_declarations(content)
        for decl in forward_decls:
            if decl in all_nodes and decl != source_node:
                dependencies[source_node].add(decl)
    
    return all_nodes, dependencies

def find_strongly_connected_components(graph, nodes):
    """Find SCCs using Tarjan's algorithm - detects cycles."""
    index_counter = [0]
    stack = []
    lowlinks = {}
    index = {}
    on_stack = {}
    sccs = []
    
    def strongconnect(node):
        index[node] = index_counter[0]
        lowlinks[node] = index_counter[0]
        index_counter[0] += 1
        stack.append(node)
        on_stack[node] = True
        
        for neighbor in graph[node]:
            if neighbor not in index:
                strongconnect(neighbor)
                lowlinks[node] = min(lowlinks[node], lowlinks[neighbor])
            elif on_stack[neighbor]:
                lowlinks[node] = min(lowlinks[node], index[neighbor])
        
        if lowlinks[node] == index[node]:
            component = []
            while True:
                w = stack.pop()
                on_stack[w] = False
                component.append(w)
                if w == node:
                    break
            sccs.append(component)
    
    for node in nodes:
        if node not in index:
            strongconnect(node)
    
    # Nodes in cycles are those in SCCs with more than 1 node,
    # or single-node SCCs that have self-loops
    cycle_nodes = set()
    for scc in sccs:
        if len(scc) > 1:
            cycle_nodes.update(scc)
        elif len(scc) == 1 and scc[0] in graph and scc[0] in graph[scc[0]]:
            # Self-loop
            cycle_nodes.add(scc[0])
    
    return cycle_nodes, sccs

def calculate_group_positions(cycle_sccs, non_cycle_nodes):
    """Calculate positions for groups and individual nodes."""
    total_groups = len(cycle_sccs) + (1 if non_cycle_nodes else 0)
    
    if total_groups == 0:
        return {}, []
    
    # Layout parameters
    center_x, center_y = 400, 400
    group_radius = max(250, total_groups * 100)
    
    group_positions = []
    current_group = 0
    
    # Position cycle groups
    for scc in cycle_sccs:
        if len(scc) <= 1:
            continue
            
        angle = 2 * math.pi * current_group / total_groups if total_groups > 1 else 0
        group_x = center_x + group_radius * math.cos(angle)
        group_y = center_y + group_radius * math.sin(angle)
        
        group_positions.append({
            'scc': scc,
            'x': group_x,
            'y': group_y,
            'type': 'cycle'
        })
        current_group += 1
    
    # Position for non-cycle nodes
    if non_cycle_nodes:
        angle = 2 * math.pi * current_group / total_groups if total_groups > 1 else 0
        group_x = center_x + group_radius * math.cos(angle)
        group_y = center_y + group_radius * math.sin(angle)
        
        group_positions.append({
            'scc': non_cycle_nodes,
            'x': group_x,
            'y': group_y,
            'type': 'individual'
        })
    
    return group_positions

def generate_graphml(nodes, dependencies, output_file, detect_cycles=False, hide_internal_edges=False):
    """Generate GraphML file for yEd with proper grouping."""
    
    # Build graph for SCC analysis
    graph = defaultdict(list)
    for source, targets in dependencies.items():
        for target in targets:
            graph[source].append(target)
    
    # Find SCCs
    cycle_nodes, sccs = find_strongly_connected_components(graph, nodes)
    
    # Identify cycle groups (SCCs with more than 1 node)
    cycle_sccs = [scc for scc in sccs if len(scc) > 1]
    non_cycle_nodes = [node for node in nodes if node not in cycle_nodes]
    
    # Calculate group positions
    group_positions = calculate_group_positions(cycle_sccs, non_cycle_nodes)
    
    # Group colors
    group_colors = [
        {"bg": "#FFE6E6", "border": "#FF0000", "name": "Cycle Group"},
        {"bg": "#E6F3FF", "border": "#0066CC", "name": "Cycle Group"},
        {"bg": "#E6FFE6", "border": "#009900", "name": "Cycle Group"},
        {"bg": "#FFF0E6", "border": "#FF6600", "name": "Cycle Group"},
        {"bg": "#F0E6FF", "border": "#9900CC", "name": "Cycle Group"},
        {"bg": "#FFFFE6", "border": "#CCCC00", "name": "Cycle Group"}
    ]
    
    # Report cycle information if requested
    if detect_cycles:
        if cycle_nodes:
            print(f"Warning: Found {len(cycle_nodes)} nodes in dependency cycles:")
            for i, scc in enumerate(cycle_sccs):
                print(f"  Cycle group {i+1}: {sorted(scc)}")
        else:
            print("No dependency cycles detected.")
    
    # Start generating GraphML
    lines = [
        '<?xml version="1.0" encoding="UTF-8" standalone="no"?>',
        '<graphml xmlns="http://graphml.graphdrawing.org/xmlns" xmlns:java="http://www.yworks.com/xml/yfiles-common/1.0/java" xmlns:sys="http://www.yworks.com/xml/yfiles-common/markup/primitives/2.0" xmlns:x="http://www.yworks.com/xml/yfiles-common/markup/2.0" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns:y="http://www.yworks.com/xml/graphml" xmlns:yed="http://www.yworks.com/xml/yed/3" xsi:schemaLocation="http://graphml.graphdrawing.org/xmlns http://www.yworks.com/xml/schema/graphml/1.1/ygraphml.xsd">',
        '  <key for="node" id="d6" yfiles.type="nodegraphics"/>',
        '  <key for="edge" id="d10" yfiles.type="edgegraphics"/>',
        '  <key attr.name="Description" attr.type="string" for="graph" id="d0"/>',
        '  <key for="node" id="d5" yfiles.type="nodegraphics"/>',
        '  <key for="edge" id="d9" yfiles.type="edgegraphics"/>',
        '  <key attr.name="Description" attr.type="string" for="node" id="d1"/>',
        '  <key attr.name="Description" attr.type="string" for="edge" id="d2"/>',
        '  <key attr.name="url" attr.type="string" for="node" id="d3"/>',
        '  <key attr.name="url" attr.type="string" for="edge" id="d4"/>',
        '  ',
        '  <graph edgedefault="directed" id="G">',
        '    <data key="d0"/>',
        '    '
    ]
    
    # Track node positions for edge generation
    node_positions = {}
    
    # Generate cycle groups
    group_id = 0
    for group_info in group_positions:
        if group_info['type'] == 'cycle':
            scc = group_info['scc']
            color = group_colors[group_id % len(group_colors)]
            
            # Calculate group dimensions
            group_width = max(200, len(scc) * 80 + 40)
            group_height = 120
            group_x = group_info['x'] - group_width / 2
            group_y = group_info['y'] - group_height / 2
            
            lines.extend([
                f'    <node id="group{group_id}">',
                f'      <data key="d6">',
                f'        <y:GroupNode>',
                f'          <y:Geometry height="{group_height:.1f}" width="{group_width:.1f}" x="{group_x:.1f}" y="{group_y:.1f}"/>',
                f'          <y:Fill color="{color["bg"]}" transparent="false"/>',
                f'          <y:BorderStyle color="{color["border"]}" type="line" width="2.0"/>',
                f'          <y:NodeLabel alignment="center" autoSizePolicy="content" fontFamily="Dialog" fontSize="12" fontStyle="bold" hasBackgroundColor="false" hasLineColor="false" modelName="internal" modelPosition="t" textColor="{color["border"]}" visible="true">{color["name"]} {group_id + 1}</y:NodeLabel>',
                f'          <y:Shape type="rectangle"/>',
                f'          <y:State closed="false" closedHeight="50.0" closedWidth="50.0" innerGraphDisplayEnabled="false"/>',
                f'          <y:Insets bottom="15" bottomF="15.0" left="15" leftF="15.0" right="15" rightF="15.0" top="15" topF="15.0"/>',
                f'          <y:BorderInsets bottom="0" bottomF="0.0" left="0" leftF="0.0" right="0" rightF="0.0" top="0" topF="0.0"/>',
                f'        </y:GroupNode>',
                f'      </data>',
                f'      <graph edgedefault="directed" id="group{group_id}:">',
                f'        '
            ])
            
            # Position nodes within the group
            for i, node in enumerate(sorted(scc)):
                node_id = sanitize_node_id(node)
                text_width = max(60, len(node) * 8 + 20)
                escaped_node = saxutils.escape(node)
                
                # Position within group bounds
                node_x = group_x + 25 + i * (text_width + 10)
                node_y = group_y + 50
                node_positions[node] = (node_x, node_y)
                
                lines.extend([
                    f'        <node id="{node_id}">',
                    f'          <data key="d6">',
                    f'            <y:ShapeNode>',
                    f'              <y:Geometry height="30.0" width="{text_width:.1f}" x="{node_x:.1f}" y="{node_y:.1f}"/>',
                    f'              <y:Fill color="#FFCC00" transparent="false"/>',
                    f'              <y:BorderStyle color="#000000" type="line" width="1.0"/>',
                    f'              <y:NodeLabel alignment="center" autoSizePolicy="content" fontFamily="Dialog" fontSize="12" fontStyle="plain" hasBackgroundColor="false" hasLineColor="false" modelName="custom" textColor="#000000" visible="true">{escaped_node}<y:LabelModel><y:SmartNodeLabelModel distance="4.0"/></y:LabelModel><y:ModelParameter><y:SmartNodeLabelModelParameter labelRatioX="0.0" labelRatioY="0.0" nodeRatioX="0.0" nodeRatioY="0.0" offsetX="0.0" offsetY="0.0" upX="0.0" upY="-1.0"/></y:ModelParameter></y:NodeLabel>',
                    f'              <y:Shape type="rectangle"/>',
                    f'            </y:ShapeNode>',
                    f'          </data>',
                    f'        </node>',
                    f'        '
                ])
            
            # Add edges within the group (only if not hiding internal edges)
            if not hide_internal_edges:
                edge_id = 0
                for source in scc:
                    if source in dependencies:
                        for target in dependencies[source]:
                            if target in scc:  # Only edges within the group
                                source_id = sanitize_node_id(source)
                                target_id = sanitize_node_id(target)
                                
                                lines.extend([
                                    f'        <edge id="group{group_id}_edge{edge_id}" source="{source_id}" target="{target_id}">',
                                    f'          <data key="d10">',
                                    f'            <y:PolyLineEdge>',
                                    f'              <y:Path sx="0.0" sy="0.0" tx="0.0" ty="0.0"/>',
                                    f'              <y:LineStyle color="#000000" type="line" width="1.0"/>',
                                    f'              <y:Arrows source="none" target="standard"/>',
                                    f'            </y:PolyLineEdge>',
                                    f'          </data>',
                                    f'        </edge>',
                                    f'        '
                                ])
                                edge_id += 1
            
            lines.extend([
                f'      </graph>',
                f'    </node>',
                f'    '
            ])
            
            group_id += 1
    
    # Generate individual nodes (non-cycle)
    for group_info in group_positions:
        if group_info['type'] == 'individual':
            nodes_list = group_info['scc']
            
            for i, node in enumerate(sorted(nodes_list)):
                node_id = sanitize_node_id(node)
                text_width = max(60, len(node) * 8 + 20)
                escaped_node = saxutils.escape(node)
                
                # Position individual nodes
                angle = 2 * math.pi * i / len(nodes_list) if len(nodes_list) > 1 else 0
                node_x = group_info['x'] + 60 * math.cos(angle)
                node_y = group_info['y'] + 60 * math.sin(angle)
                node_positions[node] = (node_x, node_y)
                
                lines.extend([
                    f'    <node id="{node_id}">',
                    f'      <data key="d6">',
                    f'        <y:ShapeNode>',
                    f'          <y:Geometry height="30.0" width="{text_width:.1f}" x="{node_x:.1f}" y="{node_y:.1f}"/>',
                    f'          <y:Fill color="#FFCC00" transparent="false"/>',
                    f'          <y:BorderStyle color="#000000" type="line" width="1.0"/>',
                    f'          <y:NodeLabel alignment="center" autoSizePolicy="content" fontFamily="Dialog" fontSize="12" fontStyle="plain" hasBackgroundColor="false" hasLineColor="false" modelName="custom" textColor="#000000" visible="true">{escaped_node}<y:LabelModel><y:SmartNodeLabelModel distance="4.0"/></y:LabelModel><y:ModelParameter><y:SmartNodeLabelModelParameter labelRatioX="0.0" labelRatioY="0.0" nodeRatioX="0.0" nodeRatioY="0.0" offsetX="0.0" offsetY="0.0" upX="0.0" upY="-1.0"/></y:ModelParameter></y:NodeLabel>',
                    f'          <y:Shape type="rectangle"/>',
                    f'        </y:ShapeNode>',
                    f'      </data>',
                    f'    </node>',
                    f'    '
                ])
    
    # Generate edges between groups and individual nodes
    edge_id = 1000  # Start with high number to avoid conflicts
    for source, targets in dependencies.items():
        for target in targets:
            # Skip edges within cycle groups (already added)
            source_group = None
            target_group = None
            
            for scc in cycle_sccs:
                if source in scc:
                    source_group = scc
                if target in scc:
                    target_group = scc
            
            # Only add edge if not within the same cycle group
            if source_group is None or target_group is None or source_group != target_group:
                source_id = sanitize_node_id(source)
                target_id = sanitize_node_id(target)
                
                lines.extend([
                    f'    <edge id="edge{edge_id}" source="{source_id}" target="{target_id}">',
                    f'      <data key="d10">',
                    f'        <y:PolyLineEdge>',
                    f'          <y:Path sx="0.0" sy="0.0" tx="0.0" ty="0.0"/>',
                    f'          <y:LineStyle color="#000000" type="line" width="1.0"/>',
                    f'          <y:Arrows source="none" target="standard"/>',
                    f'        </y:PolyLineEdge>',
                    f'      </data>',
                    f'    </edge>',
                    f'    '
                ])
                edge_id += 1
    
    # Close the GraphML
    lines.extend([
        '  </graph>',
        '</graphml>'
    ])
    
    # Write to file
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))

def main():
    parser = argparse.ArgumentParser(description='Generate dependency graph for C++ files with cycle grouping')
    parser.add_argument('folder', help='Folder to analyze (relative to script location)')
    parser.add_argument('--detect-cycles', action='store_true', help='Detect and report dependency cycles')
    parser.add_argument('--hide-internal-edges', action='store_true', help='Hide arrows between nodes within the same group')
    parser.add_argument('--skip-folders', nargs='*', default=[], help='Folders to skip (paths relative to the main folder)')
    
    args = parser.parse_args()
    
    # Get script directory
    script_dir = Path(__file__).parent.absolute()
    
    # Resolve target directory relative to script
    target_dir = script_dir / args.folder
    
    if not target_dir.exists():
        print(f"Error: Directory {target_dir} does not exist")
        sys.exit(1)
    
    if not target_dir.is_dir():
        print(f"Error: {target_dir} is not a directory")
        sys.exit(1)
    
    print(f"Analyzing C++ files in: {target_dir}")
    if args.skip_folders:
        print(f"Skipping folders: {args.skip_folders}")
    
    # Analyze dependencies
    nodes, dependencies = analyze_dependencies(target_dir, args.skip_folders)
    
    if not nodes:
        print("No C++ files found in the specified directory")
        sys.exit(1)
    
    print(f"Found {len(nodes)} files")
    print(f"Found {sum(len(deps) for deps in dependencies.values())} dependencies")
    
    # Generate output file in script directory
    output_file = script_dir / "cpp_dependencies.graphml"
    generate_graphml(nodes, dependencies, output_file, detect_cycles=args.detect_cycles, hide_internal_edges=args.hide_internal_edges)
    
    print(f"Generated GraphML file: {output_file}")
    print("Cyclically dependent nodes are grouped together in colored boxes.")
    if args.hide_internal_edges:
        print("Internal group edges are hidden for cleaner visualization.")
    print("Independent nodes are positioned separately.")
    print("You can now open this file in yEd Live to visualize the dependencies")

if __name__ == "__main__":
    main()