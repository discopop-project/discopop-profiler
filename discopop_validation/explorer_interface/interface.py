from typing import Optional, List

from ...discopop_explorer import run
from ...discopop_explorer import DetectionResult


def get_parallelization_suggestions(cu_xml: str, dep_file: str, loop_counter_file: str, reduction_file: str,
                                    plugins: List[str], file_mapping: Optional[str] = None,
                                    cu_inst_result_file: Optional[str] = None, llvm_cxxfilt_path: Optional[str] = None,
                                    discopop_build_path: Optional[str] = None, enable_task_pattern: bool = False)\
        -> DetectionResult:
    """wrapper to execute discopop_explorer and obtain a list of parallelization suggestions for further processing"""
    return run(cu_xml, dep_file, loop_counter_file, reduction_file, plugins, file_mapping, cu_inst_result_file,
               llvm_cxxfilt_path, discopop_build_path, enable_task_pattern)

