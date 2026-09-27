/* [CN-FORK] 内置 PKG Manager 基线 ELF（third_party/pkg-manager.elf）整体嵌入。
 * 用 incbin 而非 hex 数组：2MB 文件走 C 数组会显著拖慢编译。
 * 更新内置基线 = 替换 third_party/pkg-manager.elf 后重新 make，零代码改动。
 * 运行时手动上传的新版本保存在 /data/nayeplmtools/pkgmanager/，优先级高于本基线。 */
.section .rodata.pkgmanager,"a",@progbits
.balign 16
.global pkgmanager_blob
.global pkgmanager_blob_end
pkgmanager_blob:
.incbin "third_party/pkg-manager.elf"
pkgmanager_blob_end:
.section .note.GNU-stack,"",@progbits
