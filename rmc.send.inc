<?php

function rmc_run_update_check($key = NULL) {
  $key = $key ?: _rmc_get_key();
  if (!$key) {
    _rmc_write_status(RMC_STATUS_WARNING, 'RMC Update check not run. No key');
    return FALSE;
  }
  if ($available = update_get_available(TRUE)) {
    module_load_include('inc', 'update', 'update.compare');
    $data = update_calculate_project_data($available);
  }
  else {
    _rmc_write_status(RMC_STATUS_WARNING, 'Unable to get available updates');
    drupal_set_message('Unable to get available updates');
    return FALSE;
  }
  global $base_url;
  $sender_data = array(
    'send_url' => variable_get('rmc_target_address', RMC_ENV_URL),
    'project_name' => variable_get('rmc_project_name', $base_url),
    'key' => $key,
    'module_version' => '0',
    'api_version' => '0',
    'updates' => array(),
  );
  $status_list = array(
    UPDATE_NOT_SECURE,
    UPDATE_REVOKED,
    UPDATE_NOT_SUPPORTED,
    UPDATE_CURRENT,
    UPDATE_NOT_CHECKED,
    UPDATE_NOT_CURRENT
  );

  foreach ($data as $module => $module_info) {
    if (in_array($module_info['status'], $status_list, NULL)) {
      $sender_data['updates'][$module] = $data[$module];
    }
  }

  // API version
  $sender_data['api_version'] = 1;

  // Module version
  $versions = system_get_info('module', 'rmc');
  $sender_data['module_version'] = $versions['version'];

  // Expose hook to add anything else.
  drupal_alter('rmc_update_data', $sender_data);

  $response = _rmc_send_updates($sender_data);
  return $response;
}

/**
 * Helper function.
 *
 * Retrieve a key from settings.php, or from variable.
 */
function _rmc_get_key() {
  $override = variable_get('rmc_override', FALSE);
  // Key from regular configuration
  $config_key = variable_get('rmc_environment_token', NULL);
  // Key from settings.php
  $settings_key = variable_get('rmc_key', NULL);
  return ($config_key && !$override) ? $config_key : $settings_key;
}

function _rmc_send_updates($sender_data) {
  $path = $sender_data['send_url'] . RMC_URL;
  $headers = array('Content-Type' => 'application/x-www-form-urlencoded');
  $data = drupal_json_encode($sender_data);
  $options = array(
    'headers' => $headers,
    'method' => 'POST',
    'data' => http_build_query(array('data' => $data)),
  );

  $response = drupal_http_request($path, $options);

  if ($response->code != 200) {
    _rmc_write_status(RMC_STATUS_ERROR, 'Error code ' . $response->code . ' when trying to post to ' . $path);
  }
  else {
    // Check the response data, was it successful?
    $response_data = json_decode($response->data);
    if ($response_data) {
      $saved = $response_data->saved;
      if (!$saved) {
        _rmc_write_status(RMC_STATUS_ERROR, check_plain($response_data->message));
        return FALSE;
      }
      else {
        _rmc_write_status(RMC_STATUS_OK, $response_data->message);
        return TRUE;
      }
    }
  }
}

/**
 * Discontinues the process of sending updates.
 * Saves a status, message and last try time.
 *
 * @param $severity
 * @param $message
 */
function _rmc_write_status($severity, $message) {
  $message = check_plain($message);
  variable_set(RMC_STATUS, $severity);
  variable_set(RMC_STATUS_MESSAGE, $message);
  // If error, also log to watchdog.
  if ($severity == RMC_STATUS_ERROR) {
    watchdog('RMC', $message, array(), WATCHDOG_ERROR);
  }
  // If successful, set the last try timestamp
  if ($severity == RMC_STATUS_OK) {
    variable_set(RMC_LAST_TRY, time());
  }
}
