from __future__ import with_statement
from alembic import context
from alembic.script import ScriptDirectory
from alembic.operations import Operations
from sqlalchemy import engine_from_config, pool, MetaData
from sqlalchemy.ext.declarative import declarative_base
from logging.config import fileConfig
import logging

# this is the Alembic Config object, which provides
# access to the values within the .ini file in use.
config = context.config

# Interpret the config file for Python logging.
# This line sets up loggers basically.
try:
    fileConfig(config.config_file_name)
except:
    pass

## below block is needed for mssql
meta = MetaData(naming_convention = {
	"ix": 'ix_%(column_0_label)s',
	"uq": "uq_%(table_name)s_%(column_0_name)s",
	"ck": "ck_%(table_name)s_%(column_0_name)s_%(constraint_name)s",
	"fk": "fk_%(table_name)s_%(column_0_name)s_%(referred_table_name)s",
	"pk": "pk_%(table_name)s"
})
Base = declarative_base(metadata=meta)

logger = logging.getLogger('alembic.runtime.setup')
# add your model's MetaData object here
# for 'autogenerate' support
# from myapp import mymodel
# target_metadata = mymodel.Base.metadata
target_metadata = None
#Comment above line and uncomment below line for mssql
#target_metadata = Base.metadata

# other values from the config, defined by the needs of env.py,
# can be acquired:
# my_important_option = config.get_main_option("my_important_option")
# ... etc.

def run_migrations_offline():
    """Run migrations in 'offline' mode.

    This configures the context with just a URL
    and not an Engine, though an Engine is acceptable
    here as well.  By skipping the Engine creation
    we don't even need a DBAPI to be available.

    Calls to context.execute() here emit the given string to the
    script output.

    """
    url = config.get_main_option("sqlalchemy.url")
    context.configure(url=url,target_metadata=target_metadata)

    with context.begin_transaction():
        context.run_migrations()

def run_migrations_online():
    """Run migrations in 'online' mode.

    In this scenario we need to create an Engine
    and associate a connection with the context.

    """
    engine = engine_from_config(
                config.get_section(config.config_ini_section),
                prefix='sqlalchemy.',
                poolclass=pool.NullPool)

    logger.info('Testing for an old alembic_version table.')

    connection = engine.connect()
    context.configure(
                connection=connection,
                target_metadata=target_metadata,
                version_table='alembic_version'
                )

    script_location = config.get_main_option('script_location')
    found = False
    mc = context.get_context()
    current_db_revision = mc.get_current_revision()
    script = ScriptDirectory.from_config(config)
    """ If there was an existing alembic_version table, we need to
    check that it's current revision is in the history for the tree
    we're working with.
    """
    for x in script.iterate_revisions('head', 'base'):
        if x.revision == current_db_revision:
            """ An alembic_versions table was found and it belongs to
            this alembic tree
            """
            logger.info(
                ('An old alembic_version table at revision %s was '
                 'found for %s.  Renaming to alembic_version_%s.'),
                        current_db_revision, script_location,
                        script_location)
            op = Operations(mc)
            try:
                with context.begin_transaction():
                    op.rename_table(
                        'alembic_version', 'alembic_version_%s'
                        % script_location)
                found = True
            except:
                logger.error(('Unable to rename alembic_version to '
                             'alembic_version_%s.'),
                             script_location)
                connection.close()
                return

            break

    if not found:
        logger.info('Didn\'t find an old alembic_version table.')
    logger.info('Trying alembic_version_%s.' % script_location)

    """ We MAY have an alembic_version table that doesn't belong to
    this tree but if we still don't have an alembic_version_<tree>
    table, alembic will create it.
    """
    context.configure(
                connection=connection,
                target_metadata=target_metadata,
                version_table='alembic_version_' + script_location
                )
    mc = context.get_context()
    current_db_revision = mc.get_current_revision()
    if current_db_revision:
        logger.info(
            'Using the alembic_version_%s table at revision %s.',
            script_location, current_db_revision)
    else:
        logger.info('Creating new alembic_version_%s table.',
                    script_location)

    try:
        with context.begin_transaction():
            context.run_migrations()
    finally:
        connection.close()


if context.is_offline_mode():
    run_migrations_offline()
else:
    run_migrations_online()
