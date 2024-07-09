"""add queue_log option log-restricted-caller-id

Revision ID: 2b7c507d7d12
Revises: bd9c5159c7ea
Create Date: 2024-06-12 17:00:16.841343

"""

# revision identifiers, used by Alembic.
revision = '2b7c507d7d12'
down_revision = 'bd9c5159c7ea'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

AST_BOOL_NAME = 'ast_bool_values'
AST_BOOL_VALUES = [ '0', '1',
                    'off', 'on',
                    'false', 'true',
                    'no', 'yes' ]

def upgrade():
    # ast_bool_values have already been created, so use postgres enum object
    # type to get around "already created" issue - works okay with mysql
    ast_bool_values = ENUM(*AST_BOOL_VALUES, name=AST_BOOL_NAME, create_type=False)
    op.add_column('queues', sa.Column('log_restricted_caller_id', ast_bool_values))

def downgrade():
    op.drop_column('queues', 'log_restricted_caller_id')
