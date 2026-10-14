import { useEffect, useRef, useCallback } from 'react'
import { isPS5 } from '../utils/helpers.js'

export function getRouteFromHash(hash) {
  const clean = (hash || '').replace(/^#\/?/, '').trim()
  if (!clean || clean === 'dashboard') return { type: 'dashboard' }

  const [rawPath, rawQuery] = clean.split('?')
  const queryParams = new URLSearchParams(rawQuery || '')
  const mode = queryParams.get('mode') || null
  const modal = queryParams.get('modal') || null

  const extra = {}
  if (mode) extra.mode = mode
  if (modal) extra.modal = modal

  if (!rawPath || rawPath === 'dashboard') {
    return { type: 'dashboard', ...extra }
  }

  if (rawPath === 'storage') return { type: 'storage', ...extra }

  if (rawPath === 'payload' || rawPath === 'storage/payload') {
    return { type: 'storage', ...extra }
  }

  if (rawPath.startsWith('payload/') || rawPath.startsWith('storage/payload/')) {
    const slashIdx = rawPath.indexOf('payload/')
    const filename = decodeURIComponent(rawPath.slice(slashIdx + 8).trim())
    if (!filename) return { type: 'storage', ...extra }
    const srcId = queryParams.get('src') || null
    return { type: 'payload', filename, srcId, ...extra }
  }

  if (rawPath === 'autoload') return { type: 'autoload', ...extra }
  if (rawPath === 'processes') return { type: 'processes', ...extra }
  if (rawPath === 'settings') return { type: 'settings', ...extra }
  if (rawPath === 'donate') return { type: 'donate', ...extra }
  if (rawPath === 'sources') return { type: 'sources', ...extra }

  if (rawPath === 'move-from-usb' || rawPath === 'move_from_usb') {
    const path = queryParams.get('path') || null
    return { type: 'move_from_usb', path, ...extra }
  }

  if (rawPath.startsWith('move-from-usb/') || rawPath.startsWith('move_from_usb/')) {
    const slashIdx = rawPath.indexOf('/')
    const path = decodeURIComponent(rawPath.slice(slashIdx + 1).trim())
    return { type: 'move_from_usb', path, ...extra }
  }

  return { type: 'dashboard', ...extra }
}

export function formatHash(route) {
  if (!route) return '#/'
  let base = '#/'
  if (route.type === 'storage') base = '#/storage'
  else if (route.type === 'payload') {
    if (!route.filename) base = '#/storage'
    else {
      const encodedName = encodeURIComponent(route.filename)
      base = `#/payload/${encodedName}`
    }
  } else if (route.type === 'autoload') base = '#/autoload'
  else if (route.type === 'processes') base = '#/processes'
  else if (route.type === 'settings') base = '#/settings'
  else if (route.type === 'donate') base = '#/donate'
  else if (route.type === 'sources') base = '#/sources'
  else if (route.type === 'move_from_usb') {
    base = '#/move-from-usb'
  }

  const queryParams = new URLSearchParams()
  if (route.type === 'payload' && route.srcId) {
    queryParams.set('src', route.srcId)
  }
  if (route.type === 'move_from_usb' && route.path) {
    queryParams.set('path', route.path)
  }
  if (route.mode) {
    queryParams.set('mode', route.mode)
  }
  if (route.modal) {
    queryParams.set('modal', route.modal)
  }

  const queryString = queryParams.toString()
  return queryString ? `${base}?${queryString}` : base
}

export function writeHistory(route, replace = false) {
  if (typeof window === 'undefined' || !window.history || !window.history.pushState) return
  const hash = formatHash(route)
  if (!replace && window.location?.hash === hash) return
  const state = { ...route, hash }
  try {
    if (replace && window.history.replaceState) {
      window.history.replaceState(state, '', hash)
    } else {
      window.history.pushState(state, '', hash)
    }
  } catch {
    /* ignore history write errors */
  }
}

export function getHistoryChain(route) {
  if (!route || route.type === 'dashboard') {
    if (route && (route.mode || route.modal)) {
      const chain = [{ type: 'dashboard' }]
      if (route.mode && route.modal) {
        chain.push({ type: 'dashboard', mode: route.mode })
      }
      chain.push(route)
      return chain
    }
    return [{ type: 'dashboard' }]
  }

  const baseRoute = { ...route }
  delete baseRoute.modal
  delete baseRoute.mode

  let chain = []
  if (baseRoute.type === 'sources') {
    chain = [
      { type: 'dashboard' },
      { type: 'settings' },
      baseRoute,
    ]
  } else if (baseRoute.type === 'payload') {
    if (!baseRoute.filename) {
      chain = [
        { type: 'dashboard' },
        { type: 'storage' },
      ]
    } else {
      chain = [
        { type: 'dashboard' },
        { type: 'storage' },
        baseRoute,
      ]
    }
  } else if (baseRoute.type === 'move_from_usb') {
    chain = [
      { type: 'dashboard' },
      { type: 'storage' },
      baseRoute,
    ]
  } else {
    chain = [
      { type: 'dashboard' },
      baseRoute,
    ]
  }

  if (route.modal) {
    chain.push(route)
  }

  return chain
}

export function seedHistory(route) {
  if (typeof window === 'undefined' || !window.history || !window.history.pushState || !window.history.replaceState) return

  const chain = getHistoryChain(route)
  for (let i = 0; i < chain.length; i++) {
    const r = chain[i]
    const hash = formatHash(r)
    const state = { ...r, hash }
    try {
      if (i === 0 && window.history.replaceState) {
        window.history.replaceState(state, '', hash)
      } else {
        window.history.pushState(state, '', hash)
      }
    } catch {
      /* ignore history seed errors */
    }
  }
}

export function useHistoryNavigation({
  view,
  setView,
  confirmModal,
  setConfirmModal,
  downloadModal,
  setDownloadModal,
  showLogs,
  setShowLogs,
  isManageMode,
  setIsManageMode,
  isFavoriteEditMode,
  setIsFavoriteEditMode,
  selectedPayload,
  setSelectedPayload,
  moveFromUsbPath,
  setMoveFromUsbPath,
  delayModal,
  setDelayModal,
  uploadConflictModal,
  setUploadConflictModal,
  conflictPrompt,
  setConflictPrompt,
  externalLinkUrl,
  setExternalLinkUrl,
}) {
  const currentViewRef = useRef(view)
  const currentRouteRef = useRef(
    selectedPayload
      ? { type: 'payload', filename: selectedPayload.payload?.filename || selectedPayload.filename, srcId: selectedPayload.srcId || null }
      : { type: view, ...(view === 'move_from_usb' ? { path: moveFromUsbPath } : {}) }
  )
  const selectedPayloadRef = useRef(selectedPayload)
  const moveFromUsbPathRef = useRef(moveFromUsbPath)
  const historySeededRef = useRef(false)
  const historyModeRef = useRef(null)
  const historyModalRef = useRef(null)
  const isProgrammaticModalCloseRef = useRef(false)
  const initialMountTimeRef = useRef(0)
  const hasUserNavigatedRef = useRef(false)

  const modalStateRef = useRef({
    confirmModal,
    setConfirmModal,
    downloadModal,
    setDownloadModal,
    showLogs,
    setShowLogs,
    isManageMode,
    setIsManageMode,
    isFavoriteEditMode,
    setIsFavoriteEditMode,
    selectedPayload,
    setSelectedPayload,
    moveFromUsbPath,
    setMoveFromUsbPath,
    delayModal,
    setDelayModal,
    uploadConflictModal,
    setUploadConflictModal,
    conflictPrompt,
    setConflictPrompt,
    externalLinkUrl,
    setExternalLinkUrl,
  })

  useEffect(() => {
    currentViewRef.current = view
  }, [view])

  useEffect(() => {
    selectedPayloadRef.current = selectedPayload
    if (selectedPayload) {
      const filename = selectedPayload.payload?.id || selectedPayload.id || selectedPayload.payload?.filename || selectedPayload.filename
      const srcId = selectedPayload.srcId || null
      if (filename) {
        currentRouteRef.current = { type: 'payload', filename, srcId }
      }
    }
  }, [selectedPayload])

  useEffect(() => {
    moveFromUsbPathRef.current = moveFromUsbPath
  }, [moveFromUsbPath])

  useEffect(() => {
    modalStateRef.current = {
      confirmModal,
      setConfirmModal,
      downloadModal,
      setDownloadModal,
      showLogs,
      setShowLogs,
      isManageMode,
      setIsManageMode,
      isFavoriteEditMode,
      setIsFavoriteEditMode,
      selectedPayload,
      setSelectedPayload,
      moveFromUsbPath,
      setMoveFromUsbPath,
      delayModal,
      setDelayModal,
      uploadConflictModal,
      setUploadConflictModal,
      conflictPrompt,
      setConflictPrompt,
      externalLinkUrl,
      setExternalLinkUrl,
    }
  }, [
    confirmModal,
    setConfirmModal,
    downloadModal,
    setDownloadModal,
    showLogs,
    setShowLogs,
    isManageMode,
    setIsManageMode,
    isFavoriteEditMode,
    setIsFavoriteEditMode,
    selectedPayload,
    setSelectedPayload,
    moveFromUsbPath,
    setMoveFromUsbPath,
    delayModal,
    setDelayModal,
    uploadConflictModal,
    setUploadConflictModal,
    conflictPrompt,
    setConflictPrompt,
    externalLinkUrl,
    setExternalLinkUrl,
  ])

  const currentMode = (view === 'dashboard')
    ? (isManageMode ? 'manage' : isFavoriteEditMode ? 'favorites' : null)
    : null

  const currentModal = (confirmModal && confirmModal.show)
    ? 'confirm'
    : (downloadModal && downloadModal.show)
    ? 'download'
    : showLogs
    ? 'logs'
    : delayModal
    ? 'delay'
    : (uploadConflictModal && uploadConflictModal.show)
    ? 'upload_conflict'
    : conflictPrompt
    ? 'conflict'
    : externalLinkUrl
    ? 'external_link'
    : null

  // Synchronize active mode and modal with browser history so Back button on PS5 pops them cleanly
  useEffect(() => {
    if (!historySeededRef.current) return

    // 1. Sync mode
    const prevMode = historyModeRef.current
    if (currentMode !== prevMode) {
      if (currentMode && !prevMode) {
        hasUserNavigatedRef.current = true
        historyModeRef.current = currentMode
        const route = {
          ...currentRouteRef.current,
          mode: currentMode,
          modal: historyModalRef.current || null,
        }
        writeHistory(route, false)
      } else if (!currentMode && prevMode) {
        historyModeRef.current = null
        if (typeof window !== 'undefined' && window.history) {
          const hashRoute = getRouteFromHash(window.location.hash)
          if (hashRoute.mode) {
            isProgrammaticModalCloseRef.current = true
            window.history.back()
          }
        }
      } else if (currentMode && prevMode) {
        hasUserNavigatedRef.current = true
        historyModeRef.current = currentMode
        const route = {
          ...currentRouteRef.current,
          mode: currentMode,
          modal: historyModalRef.current || null,
        }
        writeHistory(route, true)
      }
    }

    // 2. Sync modal
    const prevModal = historyModalRef.current
    if (currentModal !== prevModal) {
      if (currentModal && !prevModal) {
        hasUserNavigatedRef.current = true
        historyModalRef.current = currentModal
        const route = {
          ...currentRouteRef.current,
          mode: historyModeRef.current || null,
          modal: currentModal,
        }
        writeHistory(route, false)
      } else if (!currentModal && prevModal) {
        historyModalRef.current = null
        if (typeof window !== 'undefined' && window.history) {
          const hashRoute = getRouteFromHash(window.location.hash)
          if (hashRoute.modal) {
            isProgrammaticModalCloseRef.current = true
            window.history.back()
          }
        }
      }
    }
  }, [currentMode, currentModal])

  // Seed baseline history on initial load
  useEffect(() => {
    if (historySeededRef.current) return
    historySeededRef.current = true
    initialMountTimeRef.current = Date.now()

    const hashRoute = getRouteFromHash(window.location.hash)
    const cleanRoute = { ...hashRoute }
    delete cleanRoute.modal
    delete cleanRoute.mode

    currentRouteRef.current = cleanRoute
    seedHistory(cleanRoute)

    if (cleanRoute.type === 'payload') {
      setView('storage')
      if (cleanRoute.filename) {
        setSelectedPayload({ filename: cleanRoute.filename, srcId: cleanRoute.srcId })
      }
    } else if (cleanRoute.type === 'move_from_usb') {
      if (cleanRoute.path) {
        setMoveFromUsbPath(cleanRoute.path)
        setView('move_from_usb')
      } else if (!modalStateRef.current.moveFromUsbPath) {
        setView('storage')
        writeHistory({ type: 'storage' }, true)
      } else {
        setView('move_from_usb')
      }
    } else if (cleanRoute.type !== 'dashboard') {
      setView(cleanRoute.type)
    }
  }, [setView, setSelectedPayload, setMoveFromUsbPath])

  // Handlers for payload selection / closing
  const handleClosePayload = useCallback(() => {
    hasUserNavigatedRef.current = true
    if (typeof window !== 'undefined' && window.history && window.history.length > 1 && currentRouteRef.current?.type === 'payload') {
      window.history.back()
    } else {
      if (modalStateRef.current.setSelectedPayload) {
        modalStateRef.current.setSelectedPayload(null)
      }
      selectedPayloadRef.current = null
      const route = { type: 'storage' }
      currentRouteRef.current = route
      writeHistory(route, true)
      setView('storage')
    }
  }, [setView])

  const handleSelectPayload = useCallback((payloadData) => {
    hasUserNavigatedRef.current = true
    if (!payloadData) {
      handleClosePayload()
      return
    }
    const filename = payloadData.payload?.id || payloadData.id || payloadData.payload?.filename || payloadData.filename
    const srcId = payloadData.srcId || null
    const route = { type: 'payload', filename, srcId }
    setSelectedPayload(payloadData)
    selectedPayloadRef.current = payloadData
    currentRouteRef.current = route
    writeHistory(route, false)
  }, [setSelectedPayload, handleClosePayload])

  // Listen to popstate (Circle button on PS5 controller or browser back/forward)
  useEffect(() => {
    const handlePopState = () => {
      if (isProgrammaticModalCloseRef.current) {
        isProgrammaticModalCloseRef.current = false
        return
      }

      hasUserNavigatedRef.current = true
      const m = modalStateRef.current
      const curView = currentViewRef.current
      const target = getRouteFromHash(window.location.hash)

      // 1. Dismiss any open modal first
      if (historyModalRef.current) {
        historyModalRef.current = target.modal || null

        if (m.confirmModal && m.confirmModal.show && m.setConfirmModal) {
          m.setConfirmModal({ show: false, title: '', message: '', onConfirm: null })
        }
        if (m.downloadModal && m.downloadModal.show && m.setDownloadModal) {
          m.setDownloadModal({ show: false, name: '', progress: 0 })
        }
        if (m.showLogs && m.setShowLogs) {
          m.setShowLogs(false)
        }
        if (m.delayModal && m.setDelayModal) {
          m.setDelayModal(false)
        }
        if (m.uploadConflictModal && m.uploadConflictModal.show && m.setUploadConflictModal) {
          m.setUploadConflictModal({ show: false, file: null, folderName: '' })
        }
        if (m.conflictPrompt && m.setConflictPrompt) {
          m.setConflictPrompt(null)
        }
        if (m.externalLinkUrl && m.setExternalLinkUrl) {
          m.setExternalLinkUrl(null)
        }
        return
      }

      // Defensive check for modal state even if historyModalRef was out of sync
      const anyActiveModal = Boolean(
        (m.confirmModal && m.confirmModal.show) ||
        (m.downloadModal && m.downloadModal.show) ||
        m.showLogs ||
        m.delayModal ||
        (m.uploadConflictModal && m.uploadConflictModal.show) ||
        m.conflictPrompt ||
        m.externalLinkUrl
      )
      if (anyActiveModal) {
        if (m.confirmModal && m.confirmModal.show && m.setConfirmModal) {
          m.setConfirmModal({ show: false, title: '', message: '', onConfirm: null })
        }
        if (m.downloadModal && m.downloadModal.show && m.setDownloadModal) {
          m.setDownloadModal({ show: false, name: '', progress: 0 })
        }
        if (m.showLogs && m.setShowLogs) m.setShowLogs(false)
        if (m.delayModal && m.setDelayModal) m.setDelayModal(false)
        if (m.uploadConflictModal && m.uploadConflictModal.show && m.setUploadConflictModal) {
          m.setUploadConflictModal({ show: false, file: null, folderName: '' })
        }
        if (m.conflictPrompt && m.setConflictPrompt) m.setConflictPrompt(null)
        if (m.externalLinkUrl && m.setExternalLinkUrl) m.setExternalLinkUrl(null)
        historyModalRef.current = target.modal || null
        return
      }

      // 2. Dismiss any active dashboard mode (manageMode or favoriteEditMode)
      if (curView === 'dashboard') {
        if (m.isManageMode && target.mode !== 'manage') {
          if (m.setIsManageMode) m.setIsManageMode(false)
          historyModeRef.current = target.mode || null
          return
        }
        if (m.isFavoriteEditMode && target.mode !== 'favorites') {
          if (m.setIsFavoriteEditMode) m.setIsFavoriteEditMode(false)
          historyModeRef.current = target.mode || null
          return
        }
      }

      // 3. Guard against WebKit's initial onload popstate event on PS5
      if (!hasUserNavigatedRef.current && curView === 'dashboard' && target.type === 'dashboard') {
        return
      }
      if (Date.now() - initialMountTimeRef.current < 1500 && curView === 'dashboard' && target.type === 'dashboard' && !target.mode && !target.modal) {
        return
      }

      // 4. If user was ALREADY on dashboard root with no modal/mode and target is dashboard (Circle pressed on root), close PS5 browser
      if (curView === 'dashboard' && target.type === 'dashboard' && !target.mode && !target.modal && !selectedPayloadRef.current) {
        if (hasUserNavigatedRef.current) {
          try {
            window.close()
          } catch {
            /* ignore close error */
          }
        }
        return
      }

      // 5. View transition
      currentRouteRef.current = target

      if (target.type === 'payload') {
        currentViewRef.current = 'storage'
        setView('storage')
        if (m.setSelectedPayload) {
          m.setSelectedPayload({ filename: target.filename, srcId: target.srcId })
        }
        return
      }

      if (target.type === 'storage') {
        if (m.setSelectedPayload) {
          m.setSelectedPayload(null)
        }
        if (curView === 'move_from_usb' && m.setMoveFromUsbPath) {
          m.setMoveFromUsbPath(null)
        }
        currentViewRef.current = 'storage'
        setView('storage')
        return
      }

      if (target.type === 'move_from_usb') {
        if (target.path && m.setMoveFromUsbPath) {
          m.setMoveFromUsbPath(target.path)
        } else if (!m.moveFromUsbPath) {
          currentRouteRef.current = { type: 'storage' }
          currentViewRef.current = 'storage'
          setView('storage')
          return
        }
        if (m.setSelectedPayload) m.setSelectedPayload(null)
        currentViewRef.current = 'move_from_usb'
        setView('move_from_usb')
        return
      }

      // Other views: clear payload & usb path
      if (m.setSelectedPayload) m.setSelectedPayload(null)
      if (curView === 'move_from_usb' && m.setMoveFromUsbPath) {
        m.setMoveFromUsbPath(null)
      }

      currentViewRef.current = target.type
      setView(target.type)
    }

    window.addEventListener('popstate', handlePopState)
    return () => window.removeEventListener('popstate', handlePopState)
  }, [setView])

  // Keyboard Escape dismissal for PC browsers
  useEffect(() => {
    const handleKeyDown = (e) => {
      if ((e.key === 'Escape' || e.keyCode === 27) && !isPS5) {
        const m = modalStateRef.current
        const anyModal = Boolean(
          (m.confirmModal && m.confirmModal.show) ||
          (m.downloadModal && m.downloadModal.show) ||
          m.showLogs ||
          m.delayModal ||
          (m.uploadConflictModal && m.uploadConflictModal.show) ||
          m.conflictPrompt ||
          m.externalLinkUrl
        )
        const anyMode = currentViewRef.current === 'dashboard' && (m.isManageMode || m.isFavoriteEditMode)
        if (anyModal || anyMode) {
          e.preventDefault()
          e.stopPropagation()
          if (typeof window !== 'undefined' && window.history) {
            window.history.back()
          }
        }
      }
    }

    window.addEventListener('keydown', handleKeyDown, true)
    return () => window.removeEventListener('keydown', handleKeyDown, true)
  }, [])

  // Navigate between views
  const navigate = useCallback((newView, options = {}) => {
    hasUserNavigatedRef.current = true
    if (newView === currentViewRef.current && !selectedPayloadRef.current) return
    const prevView = currentViewRef.current

    // Returning to storage while viewing a payload detail should pop history back to storage
    if (currentRouteRef.current?.type === 'payload' && newView === 'storage') {
      handleClosePayload()
      return
    }

    if (selectedPayloadRef.current) {
      if (modalStateRef.current.setSelectedPayload) {
        modalStateRef.current.setSelectedPayload(null)
      }
      selectedPayloadRef.current = null
    }

    if (newView === 'dashboard') {
      const curChain = getHistoryChain(currentRouteRef.current)
      if (curChain.length > 1 && window.history && window.history.length > 1) {
        window.history.go(-(curChain.length - 1))
        return
      }
      writeHistory({ type: 'dashboard' }, true)
      setView('dashboard')
      currentRouteRef.current = { type: 'dashboard' }
      currentViewRef.current = 'dashboard'
      return
    }

    let route = { type: newView }
    if (newView === 'move_from_usb') {
      const usbPath = options?.path || modalStateRef.current.moveFromUsbPath
      route = { type: 'move_from_usb', path: usbPath }
      if (options?.path && modalStateRef.current.setMoveFromUsbPath) {
        modalStateRef.current.setMoveFromUsbPath(options.path)
      }
    }

    if (prevView === 'dashboard') {
      writeHistory(route, false)
    } else if (newView === 'sources' || newView === 'move_from_usb') {
      writeHistory(route, false)
    } else if (prevView === 'sources' && newView === 'settings') {
      if (window.history && window.history.length > 1) {
        window.history.back()
        return
      }
      writeHistory({ type: 'settings' }, true)
    } else if (prevView === 'move_from_usb' && newView === 'storage') {
      if (window.history && window.history.length > 1) {
        window.history.back()
        return
      }
      writeHistory({ type: 'storage' }, true)
    } else {
      // Switching between top-level pages replaces state so stack remains [0: dashboard, 1: current page]
      writeHistory(route, true)
    }

    setView(newView)
    currentRouteRef.current = route
    currentViewRef.current = newView
  }, [setView, handleClosePayload])

  return { navigate, handleSelectPayload, handleClosePayload }
}
