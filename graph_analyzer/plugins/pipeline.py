from copy import deepcopy
from typing import List, Any

from graph_tool import Vertex
from graph_tool.util import find_vertex

import PETGraph
from utils import find_subnodes, correlation_coefficient, depends_ignore_readonly


total = 0
before = []
after = []


def run_before(pet):
    return pet


def run_after(pet):
    for node in find_vertex(pet.graph, pet.graph.vp.type, 'loop'):
        check_pipeline(pet, node)

    print(f'Total: {total}')
    print(" ".join([str(x) for x in before]))
    print(" ".join([str(x) for x in after]))
    return pet


def check_pipeline(pet: PETGraph, root: Vertex):
    """Tries to optimize dependencies for pipeline detection
    1. Deletes independent lines, that do not contribute to the pipeline
    2. Deletes similar CU (that have same dependencies), as those can be one step in the pipeline

    :param pet: PET graph
    :param root: current node
    :return: Pipeline scalar value
    """
    global total
    global before
    global after
    children_start_lines = [pet.graph.vp.startsAtLine[v]
                            for v in find_subnodes(pet, root, 'child')
                            if pet.graph.vp.type[v] == 'loop']
    loop_subnodes = [v for v in find_subnodes(pet, root, 'child')
                     if is_pipeline_subnode(pet, root, v, children_start_lines)]
    if len(loop_subnodes) < 3:
        return

    matrix = get_matrix(pet, root, loop_subnodes)
    initial_matrix = deepcopy(matrix)
    initial_coef = get_correlation_coefficient(matrix)

    if initial_coef < 0.999:
        total += 1
    independent_cus = get_independent_lines(matrix)
    delete_lines(matrix, loop_subnodes, independent_cus)

    no_indep_matrix = deepcopy(matrix)

    mergeable_cus = get_mergeable_nodes(matrix)
    delete_lines(matrix, loop_subnodes, mergeable_cus)

    new_coef = get_correlation_coefficient(matrix)
    if new_coef > initial_coef:
        before.append(initial_coef)
        after.append(new_coef)
        print("Pipeline improvement opportunity:")
        print("Node: " + pet.graph.vp.id[root])
        print("Lines: " + pet.graph.vp.startsAtLine[root] + "-" + pet.graph.vp.endsAtLine[root])
        print("Independent lines:")
        independent_cus.sort()
        print(" ".join([str(x) for x in independent_cus]))
        print("Similar nodes:")
        mergeable_cus.sort()
        print(" ".join([str(x) for x in mergeable_cus]))
        print(f"matrix before: {initial_coef}")
        for i in range(0, len(initial_matrix)):
            print(" ".join([str(x) for x in initial_matrix[i]]))
        print(f"matrix after independent removed:")
        for i in range(0, len(no_indep_matrix)):
            print(" ".join([str(x) for x in no_indep_matrix[i]]))
        print(f"matrix after merged: {new_coef}")
        for i in range(0, len(matrix)):
            print(" ".join([str(x) for x in matrix[i]]))


def delete_lines(matrix, loop_nodes, lines):
    if lines:
        lines.sort(reverse=True)
        for i in range(0, len(lines)):
            del loop_nodes[lines[i]]
            del matrix[lines[i]]
            for j in range(0, len(matrix)):
                del matrix[j][lines[i]]



def get_independent_lines(matrix):
    res = []
    for i in range(0, len(matrix)):
        indep = True
        for j in range(0, len(matrix)):
            if i != j and (matrix[i][j] != 0 or matrix[j][i] != 0):
                indep = False
        if indep:
            res.append(i)
    return res


def get_mergeable_nodes(matrix):
    res = []
    for i in reversed(range(1, len(matrix))):
        if matrix[i] == matrix[i - 1]:
            same = True
            for j in range(1, len(matrix)):
                if matrix[j][i] != matrix[j][i - 1]:
                    same = False
            if same:
                res.append(i)
    return res


def get_matrix(pet, root, loop_subnodes):
    res = []
    for i in range(0, len(loop_subnodes)):
        res.append([])
        for j in range(0, len(loop_subnodes)):
            res[i].append(int(depends_ignore_readonly(pet, loop_subnodes[i], loop_subnodes[j], root)))
    return res


def get_correlation_coefficient(matrix):
    graph_vector = []
    for i in range(0, len(matrix) - 1):
        graph_vector.append(matrix[i + 1][i])

    pipeline_vector = []
    for i in range(0, len(matrix) - 1):
        pipeline_vector.append(1)

    min_weight = 1
    for i in range(0, len(matrix) - 1):
        for j in range(i + 1, len(matrix)):
            if matrix[i][j] == 1:
                node_weight = 1 - (j - i) / (len(matrix) - 1)
                if min_weight > node_weight > 0:
                    min_weight = node_weight

    if min_weight == 1:
        graph_vector.append(0)
        pipeline_vector.append(0)
    else:
        graph_vector.append(1)
        pipeline_vector.append(min_weight)
    return round(correlation_coefficient(graph_vector, pipeline_vector), 2)


def is_pipeline_subnode(pet: PETGraph, root: Vertex, current: Vertex, children_start_lines: List[str]) -> bool:
    """Checks if node is a valid subnode for pipeline

    :param pet: PET graph
    :param root: root node
    :param current: current node
    :param children_start_lines: start lines of children loops
    :return: true if valid
    """
    r_start = pet.graph.vp.startsAtLine[root]
    r_end = pet.graph.vp.endsAtLine[root]
    c_start = pet.graph.vp.startsAtLine[current]
    c_end = pet.graph.vp.endsAtLine[current]
    return not (c_start == r_start and c_end == r_start
                or c_start == r_end and c_end == r_end
                or c_start == c_end and c_start in children_start_lines)